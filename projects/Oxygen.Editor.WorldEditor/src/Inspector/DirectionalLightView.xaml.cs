// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Inspector.Controls;

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

    private void OnRgbColorPicked(object? sender, InspectorRgbColorPickedEventArgs args)
    {
        if (args.Owner is DirectionalLightViewModel model)
        {
            model.SetColor(args.Color);
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
}
