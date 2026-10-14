// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

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
    }

    private void OnRgbColorPicked(object? sender, ColorBoxColorPickedEventArgs args)
        => InspectorColorEdits.Apply<DirectionalLightViewModel>(sender, args, static (model, color) => model.SetColor(color.ToVector3()));

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.ViewModel?.CompleteEditSession(args);

    private void ColorEditStarted(object? sender, ColorBoxEditSessionEventArgs args)
        => InspectorColorEdits.Begin(this.ViewModel, sender, args);

    private void ColorEditCompleted(object? sender, ColorBoxEditSessionEventArgs args)
        => InspectorColorEdits.Complete(this.ViewModel, sender, args);
}
