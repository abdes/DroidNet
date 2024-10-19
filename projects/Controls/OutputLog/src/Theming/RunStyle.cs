// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

using Microsoft.UI.Xaml.Media;
using Windows.UI;
using Windows.UI.Text;

public readonly struct RunStyle()
{
    public Brush Background { get; init; } = new SolidColorBrush(Colors.Transparent);

    public required Brush Foreground { get; init; }

    public FontWeight FontWeight { get; init; } = FontWeights.Normal;

    public FontStyle FontStyle { get; init; } = FontStyle.Normal;
}
