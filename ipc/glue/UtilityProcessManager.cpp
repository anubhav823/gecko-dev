/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "UtilityProcessManager.h"

#include "mozilla/ipc/UtilityProcessHost.h"
#include "mozilla/MemoryReportingProcess.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/SyncRunnable.h"  // for LaunchUtilityProcess
#include "mozilla/ipc/UtilityProcessParent.h"
#include "mozilla/ipc/UtilityAudioDecoderChild.h"
#include "mozilla/ipc/UtilityAudioDecoderParent.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "mozilla/ipc/ProcessChild.h"
#include "nsAppRunner.h"
#include "nsContentUtils.h"

#include "mozilla/GeckoArgs.h"

namespace mozilla::ipc {

static StaticRefPtr<UtilityProcessManager> sSingleton;

static bool sXPCOMShutdown = false;

bool UtilityProcessManager::IsShutdown() const {
  MOZ_ASSERT(NS_IsMainThread());
  return sXPCOMShutdown || !sSingleton;
}

RefPtr<UtilityProcessManager> UtilityProcessManager::GetSingleton() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  if (!sXPCOMShutdown && sSingleton == nullptr) {
    sSingleton = new UtilityProcessManager();
  }
  return sSingleton;
}

RefPtr<UtilityProcessManager> UtilityProcessManager::GetIfExists() {
  MOZ_ASSERT(NS_IsMainThread());
  return sSingleton;
}

UtilityProcessManager::UtilityProcessManager() : mObserver(new Observer(this)) {
  // Start listening for pref changes so we can
  // forward them to the process once it is running.
  nsContentUtils::RegisterShutdownObserver(mObserver);
  Preferences::AddStrongObserver(mObserver, "");
}

UtilityProcessManager::~UtilityProcessManager() {
  // The Utility process should ALL have already been shut down.
  MOZ_ASSERT(NoMoreProcesses());
}

NS_IMPL_ISUPPORTS(UtilityProcessManager::Observer, nsIObserver);

UtilityProcessManager::Observer::Observer(
    RefPtr<UtilityProcessManager> aManager)
    : mManager(std::move(aManager)) {}

NS_IMETHODIMP
UtilityProcessManager::Observer::Observe(nsISupports* aSubject,
                                         const char* aTopic,
                                         const char16_t* aData) {
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    mManager->OnXPCOMShutdown();
  } else if (!strcmp(aTopic, "nsPref:changed")) {
    mManager->OnPreferenceChange(aData);
  }
  return NS_OK;
}

void UtilityProcessManager::OnXPCOMShutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  sXPCOMShutdown = true;
  nsContentUtils::UnregisterShutdownObserver(mObserver);
  CleanShutdownAllProcesses();
}

void UtilityProcessManager::OnPreferenceChange(const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread());
  if (NoMoreProcesses()) {
    // Process hasn't been launched yet
    return;
  }
  // We know prefs are ASCII here.
  NS_LossyConvertUTF16toASCII strData(aData);

  mozilla::dom::Pref pref(strData, /* isLocked */ false,
                          /* isSanitized */ false, Nothing(), Nothing());
  Preferences::GetPreference(&pref, GeckoProcessType_Utility,
                             /* remoteType */ ""_ns);

  for (auto& p : mProcesses) {
    if (!p) {
      continue;
    }

    if (p->mProcessParent) {
      Unused << p->mProcessParent->SendPreferenceUpdate(pref);
    } else if (IsProcessLaunching(p->mSandbox)) {
      p->mQueuedPrefs.AppendElement(pref);
    }
  }
}

RefPtr<UtilityProcessManager::ProcessFields> UtilityProcessManager::GetProcess(
    SandboxingKind aSandbox) {
  if (!mProcesses[aSandbox]) {
    return nullptr;
  }

  return mProcesses[aSandbox];
}

RefPtr<GenericNonExclusivePromise> UtilityProcessManager::LaunchProcess(
    SandboxingKind aSandbox) {
  MOZ_ASSERT(NS_IsMainThread());

  if (IsShutdown()) {
    return GenericNonExclusivePromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                                       __func__);
  }

  RefPtr<ProcessFields> p = GetProcess(aSandbox);
  if (p && p->mNumProcessAttempts) {
    // We failed to start the Utility process earlier, abort now.
    return GenericNonExclusivePromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                                       __func__);
  }

  if (p && p->mLaunchPromise && p->mProcess) {
    return p->mLaunchPromise;
  }

  if (!p) {
    p = new ProcessFields(aSandbox);
    mProcesses[aSandbox] = p;
  }

  std::vector<std::string> extraArgs;
  ProcessChild::AddPlatformBuildID(extraArgs);
  geckoargs::sSandboxingKind.Put(aSandbox, extraArgs);

  // The subprocess is launched asynchronously, so we
  // wait for the promise to be resolved to acquire the IPDL actor.
  p->mProcess = new UtilityProcessHost(aSandbox, this);
  if (!p->mProcess->Launch(extraArgs)) {
    p->mNumProcessAttempts++;
    DestroyProcess(aSandbox);
    return GenericNonExclusivePromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                                       __func__);
  }

  RefPtr<UtilityProcessManager> self = this;
  p->mLaunchPromise = p->mProcess->LaunchPromise()->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self, p, aSandbox](bool) {
        if (self->IsShutdown()) {
          return GenericNonExclusivePromise::CreateAndReject(
              NS_ERROR_NOT_AVAILABLE, __func__);
        }

        if (self->IsProcessDestroyed(aSandbox)) {
          return GenericNonExclusivePromise::CreateAndReject(
              NS_ERROR_NOT_AVAILABLE, __func__);
        }

        p->mProcessParent = p->mProcess->GetActor();

        // Flush any pref updates that happened during
        // launch and weren't included in the blobs set
        // up in LaunchUtilityProcess.
        for (const mozilla::dom::Pref& pref : p->mQueuedPrefs) {
          Unused << NS_WARN_IF(!p->mProcessParent->SendPreferenceUpdate(pref));
        }
        p->mQueuedPrefs.Clear();

        CrashReporter::AnnotateCrashReport(
            CrashReporter::Annotation::UtilityProcessStatus, "Running"_ns);

        CrashReporter::AnnotateCrashReport(
            CrashReporter::Annotation::UtilityProcessSandboxingKind,
            (unsigned int)aSandbox);

        return GenericNonExclusivePromise::CreateAndResolve(true, __func__);
      },
      [self, p, aSandbox](nsresult aError) {
        if (GetSingleton()) {
          p->mNumProcessAttempts++;
          self->DestroyProcess(aSandbox);
        }
        return GenericNonExclusivePromise::CreateAndReject(aError, __func__);
      });

  return p->mLaunchPromise;
}

template <typename Actor>
RefPtr<GenericNonExclusivePromise> UtilityProcessManager::StartUtility(
    RefPtr<Actor> aActor, SandboxingKind aSandbox) {
  if (!aActor) {
    MOZ_ASSERT(false, "Actor singleton failure");
    return GenericNonExclusivePromise::CreateAndReject(NS_ERROR_FAILURE,
                                                       __func__);
  }

  if (aActor->CanSend()) {
    // Actor has already been setup, so we:
    //   - know the process has been launched
    //   - the ipc actors are ready
    return GenericNonExclusivePromise::CreateAndResolve(true, __func__);
  }

  RefPtr<UtilityProcessManager> self = this;
  return LaunchProcess(aSandbox)->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self, aActor, aSandbox]() {
        RefPtr<UtilityProcessParent> utilityParent =
            self->GetProcessParent(aSandbox);

        // It is possible if multiple processes concurrently request a utility
        // actor that the previous CanSend() check returned false for both but
        // that by the time we have started our process for real, one of them
        // has already been able to establish the IPC connection and thus we
        // would perform more than one Open() call.
        //
        // The tests within browser_utility_multipleAudio.js should be able to
        // catch that behavior.
        if (!aActor->CanSend()) {
          nsresult rv = aActor->BindToUtilityProcess(utilityParent);
          if (NS_FAILED(rv)) {
            MOZ_ASSERT(false, "Protocol endpoints failure");
            return GenericNonExclusivePromise::CreateAndReject(rv, __func__);
          }

          MOZ_DIAGNOSTIC_ASSERT(aActor->CanSend(), "IPC established for actor");
          self->RegisterActor(utilityParent, aActor->GetActorName());
        }

        return GenericNonExclusivePromise::CreateAndResolve(true, __func__);
      },
      [](nsresult aError) {
        MOZ_ASSERT_UNREACHABLE("Failure when starting actor");
        return GenericNonExclusivePromise::CreateAndReject(aError, __func__);
      });
}

RefPtr<UtilityProcessManager::AudioDecodingPromise>
UtilityProcessManager::StartAudioDecoding(base::ProcessId aOtherProcess) {
  RefPtr<UtilityProcessManager> self = this;
  RefPtr<UtilityAudioDecoderChild> uadc =
      UtilityAudioDecoderChild::GetSingleton();
  MOZ_ASSERT(uadc, "Unable to get a singleton for UtilityAudioDecoderChild");
  return StartUtility(uadc, SandboxingKind::UTILITY_AUDIO_DECODING)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self, uadc, aOtherProcess]() {
            base::ProcessId process =
                self->GetProcessParent(SandboxingKind::UTILITY_AUDIO_DECODING)
                    ->OtherPid();

            if (!uadc->CanSend()) {
              MOZ_ASSERT(false, "UtilityAudioDecoderChild lost in the middle");
              return AudioDecodingPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                           __func__);
            }

            Endpoint<PRemoteDecoderManagerChild> childPipe;
            Endpoint<PRemoteDecoderManagerParent> parentPipe;
            nsresult rv = PRemoteDecoderManager::CreateEndpoints(
                process, aOtherProcess, &parentPipe, &childPipe);
            if (NS_FAILED(rv)) {
              MOZ_ASSERT(false, "Could not create content remote decoder");
              return AudioDecodingPromise::CreateAndReject(rv, __func__);
            }

            if (!uadc->SendNewContentRemoteDecoderManager(
                    std::move(parentPipe))) {
              MOZ_ASSERT(false, "SendNewContentRemoteDecoderManager failure");
              return AudioDecodingPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                           __func__);
            }

            return AudioDecodingPromise::CreateAndResolve(std::move(childPipe),
                                                          __func__);
          },
          [](nsresult aError) {
            MOZ_ASSERT_UNREACHABLE(
                "PUtilityAudioDecoder: failure when starting actor");
            return AudioDecodingPromise::CreateAndReject(aError, __func__);
          });
}

bool UtilityProcessManager::IsProcessLaunching(SandboxingKind aSandbox) {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<ProcessFields> p = GetProcess(aSandbox);
  if (!p) {
    MOZ_CRASH("Cannot check process launching with no process");
    return false;
  }

  return p->mProcess && !(p->mProcessParent);
}

bool UtilityProcessManager::IsProcessDestroyed(SandboxingKind aSandbox) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<ProcessFields> p = GetProcess(aSandbox);
  if (!p) {
    MOZ_CRASH("Cannot check process destroyed with no process");
    return false;
  }
  return !p->mProcess && !p->mProcessParent;
}

void UtilityProcessManager::OnProcessUnexpectedShutdown(
    UtilityProcessHost* aHost) {
  MOZ_ASSERT(NS_IsMainThread());

  for (auto& it : mProcesses) {
    if (it && it->mProcess && it->mProcess == aHost) {
      it->mNumUnexpectedCrashes++;
      DestroyProcess(it->mSandbox);
      return;
    }
  }

  MOZ_CRASH(
      "Called UtilityProcessManager::OnProcessUnexpectedShutdown with invalid "
      "aHost");
}

void UtilityProcessManager::CleanShutdownAllProcesses() {
  for (auto& it : mProcesses) {
    if (it) {
      DestroyProcess(it->mSandbox);
    }
  }
}

void UtilityProcessManager::CleanShutdown(SandboxingKind aSandbox) {
  DestroyProcess(aSandbox);
}

uint16_t UtilityProcessManager::AliveProcesses() {
  uint16_t alive = 0;
  for (auto& p : mProcesses) {
    if (p != nullptr) {
      alive++;
    }
  }
  return alive;
}

bool UtilityProcessManager::NoMoreProcesses() { return AliveProcesses() == 0; }

void UtilityProcessManager::DestroyProcess(SandboxingKind aSandbox) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  if (AliveProcesses() <= 1) {
    if (mObserver) {
      Preferences::RemoveObserver(mObserver, "");
    }

    mObserver = nullptr;
    sSingleton = nullptr;
  }

  RefPtr<ProcessFields> p = GetProcess(aSandbox);
  if (!p) {
    MOZ_CRASH("Cannot get no ProcessFields");
    return;
  }

  p->mQueuedPrefs.Clear();
  p->mProcessParent = nullptr;

  if (!p->mProcess) {
    return;
  }

  p->mProcess->Shutdown();
  p->mProcess = nullptr;

  mProcesses[aSandbox] = nullptr;

  CrashReporter::AnnotateCrashReport(
      CrashReporter::Annotation::UtilityProcessStatus, "Destroyed"_ns);
}

Maybe<base::ProcessId> UtilityProcessManager::ProcessPid(
    SandboxingKind aSandbox) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<ProcessFields> p = GetProcess(aSandbox);
  if (!p) {
    return Nothing();
  }
  if (p->mProcessParent) {
    return Some(p->mProcessParent->OtherPid());
  }
  return Nothing();
}

class UtilityMemoryReporter : public MemoryReportingProcess {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(UtilityMemoryReporter, override)

  explicit UtilityMemoryReporter(UtilityProcessParent* aParent) {
    mParent = aParent;
  }

  bool IsAlive() const override { return bool(GetParent()); }

  bool SendRequestMemoryReport(
      const uint32_t& aGeneration, const bool& aAnonymize,
      const bool& aMinimizeMemoryUsage,
      const Maybe<ipc::FileDescriptor>& aDMDFile) override {
    RefPtr<UtilityProcessParent> parent = GetParent();
    if (!parent) {
      return false;
    }

    return parent->SendRequestMemoryReport(aGeneration, aAnonymize,
                                           aMinimizeMemoryUsage, aDMDFile);
  }

  int32_t Pid() const override {
    if (RefPtr<UtilityProcessParent> parent = GetParent()) {
      return (int32_t)parent->OtherPid();
    }
    return 0;
  }

 private:
  RefPtr<UtilityProcessParent> GetParent() const { return mParent; }

  RefPtr<UtilityProcessParent> mParent = nullptr;

 protected:
  ~UtilityMemoryReporter() = default;
};

RefPtr<MemoryReportingProcess> UtilityProcessManager::GetProcessMemoryReporter(
    UtilityProcessParent* parent) {
  return new UtilityMemoryReporter(parent);
}

}  // namespace mozilla::ipc
