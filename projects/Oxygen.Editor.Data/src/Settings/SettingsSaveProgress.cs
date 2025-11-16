// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Represents the progress of saving settings, including total settings, completed settings, and the current module.
/// </summary>
/// <param name="TotalSettings">The total number of settings to save.</param>
/// <param name="CompletedSettings">The number of settings that have been saved.</param>
/// <param name="CurrentModule">The name of the module currently being saved.</param>
public readonly record struct SettingsSaveProgress(int TotalSettings, int CompletedSettings, string CurrentModule);
