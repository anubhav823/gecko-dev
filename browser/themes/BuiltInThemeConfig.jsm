/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = ["BuiltInThemeConfig"];

const { XPCOMUtils } = ChromeUtils.import(
  "resource://gre/modules/XPCOMUtils.jsm"
);
XPCOMUtils.defineLazyModuleGetters(this, {
  AppConstants: "resource://gre/modules/AppConstants.jsm",
});

/**
 * A Map of themes built in to the browser, alongwith a Map of collections those themes belong to. Params for the objects contained
 * within the map:
 * @param {string} id
 *   The unique identifier for the theme. The map's key.
 * @param {string} version
 *   The theme add-on's semantic version, as defined in its manifest.
 * @param {string} path
 *   Path to the add-on files.
 * @param {string} [expiry]
 *  Date in YYYY-MM-DD format. Optional. If defined, the themes in the collection can no longer be
 *  used after this date, unless the user has permission to retain it.
 * @param {string} [collection]
 *  The collection id that the theme is a part of. Optional.
 */
const _BuiltInThemeConfig = new Map([
  [
    "firefox-compact-light@mozilla.org",
    {
      version: "1.2",
      path: "resource://builtin-themes/light/",
    },
  ],
  [
    "firefox-compact-dark@mozilla.org",
    {
      version: "1.2",
      path: "resource://builtin-themes/dark/",
    },
  ],
  [
    "firefox-alpenglow@mozilla.org",
    {
      version: "1.4",
      path: "resource://builtin-themes/alpenglow/",
    },
  ],
  [
    "2022red-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/2022red/",
      collection: "true-colors",
    },
  ],
  [
    "2022orange-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/2022orange/",
      collection: "true-colors",
    },
  ],
  [
    "2022green-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/2022green/",
      collection: "true-colors",
    },
  ],
  [
    "2022yellow-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/2022yellow/",
      collection: "true-colors",
    },
  ],
  [
    "2022purple-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/2022purple/",
      collection: "true-colors",
    },
  ],
  [
    "2022blue-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/2022blue/",
      collection: "true-colors",
    },
  ],
  [
    "lush-soft-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/lush/soft/",
      collection: "life-in-color",
    },
  ],
  [
    "lush-balanced-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/lush/balanced/",
      collection: "life-in-color",
    },
  ],
  [
    "lush-bold-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/lush/bold/",
      collection: "life-in-color",
    },
  ],
  [
    "abstract-soft-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/abstract/soft/",
      collection: "life-in-color",
    },
  ],
  [
    "abstract-balanced-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/abstract/balanced/",
      collection: "life-in-color",
    },
  ],
  [
    "abstract-bold-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/abstract/bold/",
      collection: "life-in-color",
    },
  ],
  [
    "elemental-soft-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/elemental/soft/",
      collection: "life-in-color",
    },
  ],
  [
    "elemental-balanced-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/elemental/balanced/",
      collection: "life-in-color",
    },
  ],
  [
    "elemental-bold-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/elemental/bold/",
      collection: "life-in-color",
    },
  ],
  [
    "cheers-soft-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/cheers/soft/",
      collection: "life-in-color",
    },
  ],
  [
    "cheers-balanced-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/cheers/balanced/",
      collection: "life-in-color",
    },
  ],
  [
    "cheers-bold-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/cheers/bold/",
      collection: "life-in-color",
    },
  ],
  [
    "graffiti-soft-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/graffiti/soft/",
      collection: "life-in-color",
    },
  ],
  [
    "graffiti-balanced-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/graffiti/balanced/",
      collection: "life-in-color",
    },
  ],
  [
    "graffiti-bold-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/graffiti/bold/",
      collection: "life-in-color",
    },
  ],
  [
    "foto-soft-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/foto/soft/",
      collection: "life-in-color",
    },
  ],
  [
    "foto-balanced-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/foto/balanced/",
      collection: "life-in-color",
    },
  ],
  [
    "foto-bold-colorway@mozilla.org",
    {
      version: "1.0",
      path: "resource://builtin-themes/monochromatic/foto/bold/",
      collection: "life-in-color",
    },
  ],
]);

const ColorwayCollections = new Map([
  [
    "life-in-color",
    {
      expiry: AppConstants.NIGHTLY_BUILD ? "2022-08-03" : "2022-02-08",
    },
  ],
  [
    "true-colors",
    {
      expiry: AppConstants.NIGHTLY_BUILD ? "2022-4-20" : "2022-05-03",
    },
  ],
]);

const BuiltInThemeConfig = new Map();
for (let [key, config] of _BuiltInThemeConfig.entries()) {
  if (config.collection) {
    config.expiry = ColorwayCollections.get(config.collection).expiry;
  }
  BuiltInThemeConfig.set(key, config);
}
