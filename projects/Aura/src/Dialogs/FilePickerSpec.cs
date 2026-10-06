// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Dialogs;

/// <summary>Describes a desktop file-open picker with labeled extension groups.</summary>
/// <param name="Title">The picker title.</param>
/// <param name="SettingsIdentifier">The stable identifier for the picker's remembered location.</param>
/// <param name="FileTypeChoices">The labels and allowed extensions, in display order.</param>
public sealed record FilePickerSpec(
    string Title,
    string SettingsIdentifier,
    IReadOnlyDictionary<string, IList<string>> FileTypeChoices)
{
    /// <summary>Gets the initial folder used when the picker has no remembered location.</summary>
    public string SuggestedStartFolder { get; init; } = string.Empty;
}
