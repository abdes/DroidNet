// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Generic progress report for settings operations. Used for save/load/batch progress.
/// </summary>
/// <param name="TotalSettings">The total number of items to operate on.</param>
/// <param name="CompletedSettings">The number of items already completed.</param>
/// <param name="SettingModule">The name of the module that the settings belong to (for grouping/tracking).</param>
/// <param name="SettingName">The optional name of the specific setting being processed; null when the progress applies to multiple or unspecified settings.</param>
public readonly record struct SettingsProgress(int TotalSettings, int CompletedSettings, string SettingModule, string? SettingName);
