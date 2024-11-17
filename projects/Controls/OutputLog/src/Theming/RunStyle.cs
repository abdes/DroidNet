// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml.Media;
using Windows.UI.Text;

namespace DroidNet.Controls.OutputLog.Theming;

/// <summary>
/// Represents the style settings for a text run in a rich text control.
/// </summary>
/// <remarks>
/// <para>
/// This struct is used to define the styling properties for a text run, such as foreground color,
/// background color, font weight, and font style. It ensures that text runs are styled consistently
/// according to the specified settings.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use this struct to define the styling for text runs in rich text controls. This can be useful for
/// applying consistent theming to log messages or other text content.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var runStyle = new RunStyle
/// {
///     Foreground = new SolidColorBrush(Colors.Red),
///     FontWeight = FontWeights.Bold,
///     FontStyle = FontStyle.Italic
/// };
/// var run = new Run { Text = "Error: Something went wrong!" };
/// run.Foreground = runStyle.Foreground;
/// run.FontWeight = runStyle.FontWeight;
/// run.FontStyle = runStyle.FontStyle;
/// // The run is styled with the specified settings
/// ]]></code>
/// </para>
/// </remarks>
public readonly struct RunStyle() : IEquatable<RunStyle>
{
    /// <summary>
    /// Gets the background brush for the text run.
    /// </summary>
    /// <value>The background brush. The default is a transparent brush.</value>
    public Brush Background { get; init; } = new SolidColorBrush(Colors.Transparent);

    /// <summary>
    /// Gets the foreground brush for the text run.
    /// </summary>
    /// <value>The foreground brush. The default is a black brush.</value>
    public required Brush Foreground { get; init; } = new SolidColorBrush(Colors.Black);

    /// <summary>
    /// Gets the font weight for the text run.
    /// </summary>
    /// <value>The font weight. The default is <see cref="FontWeights.Normal"/>.</value>
    public FontWeight FontWeight { get; init; } = FontWeights.Normal;

    /// <summary>
    /// Gets the font style for the text run.
    /// </summary>
    /// <value>The font style. The default is <see cref="FontStyle.Normal"/>.</value>
    public FontStyle FontStyle { get; init; } = FontStyle.Normal;

    /// <summary>
    /// Determines whether two specified instances of <see cref="RunStyle"/> are equal.
    /// </summary>
    /// <param name="left">The first <see cref="RunStyle"/> to compare.</param>
    /// <param name="right">The second <see cref="RunStyle"/> to compare.</param>
    /// <returns><see langword="true"/> if the two <see cref="RunStyle"/> instances are equal; otherwise, <see langword="false"/>.</returns>
    public static bool operator ==(RunStyle left, RunStyle right) => left.Equals(right);

    /// <summary>
    /// Determines whether two specified instances of <see cref="RunStyle"/> are not equal.
    /// </summary>
    /// <param name="left">The first <see cref="RunStyle"/> to compare.</param>
    /// <param name="right">The second <see cref="RunStyle"/> to compare.</param>
    /// <returns><see langword="true"/> if the two <see cref="RunStyle"/> instances are not equal; otherwise, <see langword="false"/>.</returns>
    public static bool operator !=(RunStyle left, RunStyle right) => !(left == right);

    /// <summary>
    /// Determines whether the specified object is equal to the current <see cref="RunStyle"/>.
    /// </summary>
    /// <param name="obj">The object to compare with the current <see cref="RunStyle"/>.</param>
    /// <returns><see langword="true"/> if the specified object is equal to the current <see cref="RunStyle"/>; otherwise, <see langword="false"/>.</returns>
    public override bool Equals(object? obj) => obj is RunStyle style && this.Equals(style);

    /// <summary>
    /// Determines whether the specified <see cref="RunStyle"/> is equal to the current <see cref="RunStyle"/>.
    /// </summary>
    /// <param name="other">The <see cref="RunStyle"/> to compare with the current <see cref="RunStyle"/>.</param>
    /// <returns><see langword="true"/> if the specified <see cref="RunStyle"/> is equal to the current <see cref="RunStyle"/>; otherwise, <see langword="false"/>.</returns>
    public bool Equals(RunStyle other) => EqualityComparer<Brush>.Default.Equals(this.Background, other.Background) &&
                                          EqualityComparer<Brush>.Default.Equals(this.Foreground, other.Foreground) &&
                                          this.FontWeight.Equals(other.FontWeight) && this.FontStyle == other.FontStyle;

    /// <summary>
    /// Serves as the default hash function.
    /// </summary>
    /// <returns>A hash code for the current <see cref="RunStyle"/>.</returns>
    public override int GetHashCode() => HashCode.Combine(
        this.Background,
        this.Foreground,
        this.FontWeight,
        this.FontStyle);
}
