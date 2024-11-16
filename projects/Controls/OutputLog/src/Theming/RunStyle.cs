// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml.Media;
using Windows.UI.Text;

public readonly struct RunStyle() : IEquatable<RunStyle>
{
    public Brush Background { get; init; } = new SolidColorBrush(Colors.Transparent);

    public required Brush Foreground { get; init; } = new SolidColorBrush(Colors.Black);

    public FontWeight FontWeight { get; init; } = FontWeights.Normal;

    public FontStyle FontStyle { get; init; } = FontStyle.Normal;

    public static bool operator ==(RunStyle left, RunStyle right) => left.Equals(right);

    public static bool operator !=(RunStyle left, RunStyle right) => !(left == right);

    public override bool Equals(object? obj) => obj is RunStyle style && this.Equals(style);

    public bool Equals(RunStyle other) => EqualityComparer<Brush>.Default.Equals(this.Background, other.Background) &&
                                          EqualityComparer<Brush>.Default.Equals(this.Foreground, other.Foreground) &&
                                          this.FontWeight.Equals(other.FontWeight) && this.FontStyle == other.FontStyle;

    public override int GetHashCode() => HashCode.Combine(
        this.Background,
        this.Foreground,
        this.FontWeight,
        this.FontStyle);
}
