// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>The settings of the selected files changed through a row of the import settings panel.</summary>
/// <param name="label">What changed, as a history step names it, such as "Edit Units".</param>
public sealed class SettingEditedEventArgs(string label) : EventArgs
{
    /// <summary>Gets what changed, as a history step names it.</summary>
    public string Label { get; } = label;
}
