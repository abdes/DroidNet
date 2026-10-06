// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Dialogs;

/// <summary>Describes an unrestricted desktop folder picker.</summary>
/// <param name="Title">The picker title.</param>
/// <param name="SettingsIdentifier">The stable identifier for the picker's remembered location.</param>
public sealed record FolderPickerSpec(string Title, string SettingsIdentifier)
{
    /// <summary>Gets the initial folder used when the picker has no remembered location.</summary>
    public string SuggestedStartFolder { get; init; } = string.Empty;
}
