// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Local fog volume inspector view.</summary>
[ViewModel(typeof(LocalFogVolumeViewModel))]
public sealed partial class LocalFogVolumeView
{
    /// <summary>Initializes a new instance of the <see cref="LocalFogVolumeView"/> class.</summary>
    public LocalFogVolumeView() => this.InitializeComponent();

    private void OnAlbedoPicked(object? sender, ColorBoxColorPickedEventArgs args)
        => InspectorColorEdits.Apply<LocalFogVolumeViewModel>(sender, args, static (model, color) => model.SetAlbedo(color.ToVector3()));

    private void OnEmissivePicked(object? sender, ColorBoxColorPickedEventArgs args)
        => InspectorColorEdits.Apply<LocalFogVolumeViewModel>(sender, args, static (model, color) => model.SetEmissive(color.ToVector3()));

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
