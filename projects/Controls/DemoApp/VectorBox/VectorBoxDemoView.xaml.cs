// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Mvvm.Generators;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.Demo.InPlaceEdit;

/// <summary>
/// A demo page for the <see cref="VectorBox" /> control: two to four components, labels, masks, validation, mixed
/// values and every presentation option.
/// </summary>
[ViewModel(typeof(VectorBoxDemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Views with injectable ViewModel must be public")]
public sealed partial class VectorBoxDemoView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="VectorBoxDemoView" /> class.
    /// </summary>
    public VectorBoxDemoView()
    {
        this.InitializeComponent();

        foreach (var vector in (ReadOnlySpan<VectorBox>)[this.PositionVector, this.RotationVector, this.ScaleVector, this.OrientationVector])
        {
            vector.ComponentLabelForegrounds[Component.X] = new SolidColorBrush(Colors.Red);
            vector.ComponentLabelForegrounds[Component.Y] = new SolidColorBrush(Colors.Green);
            vector.ComponentLabelForegrounds[Component.Z] = new SolidColorBrush(Colors.Blue);
        }

        this.TilingVector.ComponentLabels[Component.X] = "U";
        this.TilingVector.ComponentLabels[Component.Y] = "V";
        this.MixedMaskVector.ComponentMasks[Component.X] = "~.# m";
        this.MixedMaskVector.ComponentMasks[Component.Y] = "##.##";
        this.MixedMaskVector.ComponentMasks[Component.Z] = "~.###";
    }

    // A RadioButtons group also reports the moment it has nothing selected; only a newly selected item applies.
    private static bool TrySelectedText(SelectionChangedEventArgs args, out string text)
    {
        text = args.AddedItems is [string selected] ? selected : string.Empty;
        return text.Length != 0;
    }

    private void OnConstrainedValidate(object? sender, ValidationEventArgs<double> args)
        => args.IsValid = args.NewValue is >= -180d and <= 180d;

    private void OnDimensionChanged(object sender, SelectionChangedEventArgs args)
    {
        if (TrySelectedText(args, out var text))
        {
            this.ConfigurableVector.Dimension = int.Parse(text, CultureInfo.InvariantCulture);
        }
    }

    private void OnLabelPositionChanged(object sender, SelectionChangedEventArgs args)
    {
        if (TrySelectedText(args, out var text))
        {
            this.ConfigurableVector.LabelPosition = Enum.Parse<LabelPosition>(text);
        }
    }

    private void OnComponentLabelPositionChanged(object sender, SelectionChangedEventArgs args)
    {
        if (TrySelectedText(args, out var text))
        {
            this.ConfigurableVector.ComponentLabelPosition = Enum.Parse<LabelPosition>(text);
        }
    }

    private void OnMaskChanged(object sender, SelectionChangedEventArgs args)
    {
        if (TrySelectedText(args, out var text))
        {
            this.ConfigurableVector.ComponentMask = text;
        }
    }

    private void OnMultiplierChanged(object sender, SelectionChangedEventArgs args)
    {
        if (TrySelectedText(args, out var text))
        {
            this.ConfigurableVector.Multiplier = int.Parse(text, CultureInfo.InvariantCulture);
        }
    }

    private void OnValueAlignmentChanged(object sender, SelectionChangedEventArgs args)
    {
        if (TrySelectedText(args, out var text))
        {
            this.ConfigurableVector.HorizontalValueAlignment = Enum.Parse<TextAlignment>(text);
        }
    }
}
