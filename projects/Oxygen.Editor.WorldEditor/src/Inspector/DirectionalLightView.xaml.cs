// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Directional light inspector view.
/// </summary>
[ViewModel(typeof(DirectionalLightViewModel))]
public sealed partial class DirectionalLightView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="DirectionalLightView"/> class.
    /// </summary>
    public DirectionalLightView()
    {
        this.InitializeComponent();
        InspectorRgbPresentation.Configure(this.ColorChannels);
        InspectorRgbPresentation.Configure(this.DiskScaleChannels);
    }

    private void ColorPicker_ColorChanged(ColorPicker sender, ColorChangedEventArgs args)
    {
        if (this.ViewModel is { } model && InspectorRgbPresentation.ToDisplayColor(model.ColorValue) != args.NewColor)
        {
            InspectorColorGestures.Apply(sender, owner => ((DirectionalLightViewModel)owner).SetColor(InspectorRgbPresentation.ToLinearRgb(args.NewColor)));
        }
    }

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.ViewModel?.CompleteEditSession(args);

    private void VectorEditStarted(object? sender, VectorBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            var channel = args.Component switch { Component.X => "R", Component.Y => "G", _ => "B" };
            this.ViewModel?.BeginEditSession($"{field}{channel}", args.InteractionKind);
        }
    }

    private void VectorEditCompleted(object? sender, VectorBoxEditSessionEventArgs args)
        => this.ViewModel?.CompleteEditSession(new(args.InteractionKind, args.CompletionKind));

    private void ColorPickerLoaded(object sender, RoutedEventArgs args)
    {
        if (sender is ColorPicker picker)
        {
            InspectorColorGestures.Attach(picker, this.ViewModel, "Color");
        }
    }
}
