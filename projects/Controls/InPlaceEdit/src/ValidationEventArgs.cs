// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Provides data for the <see cref="InPlaceEditableLabel.Validate"/> event.
/// </summary>
/// <param name="text">The text to be validated.</param>
public class ValidationEventArgs(string text) : EventArgs
{
    /// <summary>
    /// Gets the text to be validated.
    /// </summary>
    public string Text { get; } = text;

    /// <summary>
    /// Gets or sets a value indicating whether the text is valid.
    /// </summary>
    /// <value><see langword="true"/> if the text is valid; otherwise, <see langword="false"/>.</value>
    public bool IsValid { get; set; } = true;
}
