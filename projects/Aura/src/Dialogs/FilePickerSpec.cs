// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Dialogs;

/// <summary>Describes a desktop file-open picker with labeled extension groups.</summary>
/// <param name="Title">The picker title.</param>
/// <param name="SettingsIdentifier">The stable identifier for the picker's remembered location.</param>
/// <param name="FileTypeChoices">
///     The labels and allowed extensions, in display order. An extension may be written <c>.png</c>, <c>png</c> or
///     <c>*.png</c>.
/// </param>
public sealed record FilePickerSpec(
    string Title,
    string SettingsIdentifier,
    IReadOnlyDictionary<string, IReadOnlyList<string>> FileTypeChoices)
{
    /// <summary>
    ///     Gets the initial folder used when the picker has no remembered location; <see langword="null"/> to use the
    ///     picker's own default.
    /// </summary>
    public string? SuggestedStartFolder { get; init; }
}
