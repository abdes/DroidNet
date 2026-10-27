// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>Scene Instance section view.</summary>
[ViewModel(typeof(SceneInstanceViewModel))]
public sealed partial class SceneInstanceView
{
    private SceneInstanceViewModel? observedModel;

    /// <summary>Initializes a new instance of the <see cref="SceneInstanceView"/> class.</summary>
    public SceneInstanceView()
    {
        this.InitializeComponent();
        this.Loaded += (_, _) => this.ObserveModel();
        this.Unloaded += (_, _) => this.StopObservingModel();
        this.ViewModelChanged += (_, _) => this.ObserveModel();
    }

    private async void OnSourcePickerOpening(object? sender, EventArgs args)
    {
        _ = sender;
        _ = args;
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when the source picker opens.");
        await this.ViewModel.PrepareChoicesAsync().ConfigureAwait(true);
    }

    private async void OnSourceChoiceInvoked(object? sender, AssetChoice choice)
    {
        _ = sender;
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when handling a source choice.");
        if (choice.Asset is { } source)
        {
            await this.ViewModel.SwapAsync(source).ConfigureAwait(true);
        }
    }

    private async void OnApplyClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when applying.");
        await this.ViewModel.ApplyAsync().ConfigureAwait(true);
    }

    private async void OnUnpackClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when unpacking.");
        await this.ViewModel.UnpackAsync(InstanceUnpack.One).ConfigureAwait(true);
    }

    private async void OnUnpackCompletelyClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when unpacking.");
        await this.ViewModel.UnpackAsync(InstanceUnpack.Complete).ConfigureAwait(true);
    }

    private async void OnReassignClick(object sender, RoutedEventArgs args)
    {
        _ = args;
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when reassigning.");
        if (sender is not FrameworkElement { DataContext: KeptChangeRow row } button)
        {
            return;
        }

        if (this.ViewModel.ChoicesOf(row) is not { } choices)
        {
            return;
        }

        var picker = new KeptChangeTargetPicker(choices);
        var flyout = new Flyout { Content = picker, Placement = FlyoutPlacementMode.BottomEdgeAlignedRight };
        var chosen = new TaskCompletionSource<NodePath?>();
        picker.Chosen += (_, args) =>
        {
            _ = chosen.TrySetResult(args.Target);
            flyout.Hide();
        };
        flyout.Closed += (_, _) => chosen.TrySetResult(result: null);
        flyout.ShowAt(button);
        if (await chosen.Task.ConfigureAwait(true) is { } path)
        {
            await this.ViewModel.ReassignAsync(row, path).ConfigureAwait(true);
        }
    }

    private async void OnRemoveKeptClick(object sender, RoutedEventArgs args)
    {
        _ = args;
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when removing a kept change.");
        if (sender is FrameworkElement { DataContext: KeptChangeRow row })
        {
            await this.ViewModel.RemoveKeptChangesAsync([row]).ConfigureAwait(true);
        }
    }

    private async void OnRemoveAllKeptClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when removing kept changes.");
        await this.ViewModel.RemoveKeptChangesAsync([.. this.ViewModel.KeptChangeRows]).ConfigureAwait(true);
    }

    private void ObserveModel()
    {
        this.StopObservingModel();
        if (this.IsLoaded && this.ViewModel is { } model)
        {
            this.observedModel = model;
            model.KeptChangesFocusRequested += this.OnKeptChangesFocusRequested;
        }
    }

    private void StopObservingModel()
    {
        if (this.observedModel is { } model)
        {
            model.KeptChangesFocusRequested -= this.OnKeptChangesFocusRequested;
            this.observedModel = null;
        }
    }

    private void OnKeptChangesFocusRequested(object? sender, EventArgs args)
    {
        _ = sender;
        _ = args;
        this.KeptChangesBox.StartBringIntoView();
    }
}
