// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Pickers;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Composes typed asset pickers and retained scene-reference rows.</summary>
[ViewModel(typeof(SceneReferencesSectionViewModel))]
public sealed partial class SceneReferencesSectionView
{
    /// <summary>Initializes a new instance of the <see cref="SceneReferencesSectionView"/> class.</summary>
    public SceneReferencesSectionView() => this.InitializeComponent();

    private async void OnScriptChoiceInvoked(object? sender, AssetChoice choice)
    {
        _ = sender;
        _ = choice;

        await this.AddReferenceAsync(AssetKind.Script, choice, this.AddScriptFlyout).ConfigureAwait(true);
    }

    private async void OnInputActionChoiceInvoked(object? sender, AssetChoice choice)
    {
        _ = sender;
        _ = choice;

        await this.AddReferenceAsync(AssetKind.InputAction, choice, this.AddInputActionFlyout).ConfigureAwait(true);
    }

    private async void OnPhysicsSidecarChoiceInvoked(object? sender, AssetChoice choice)
    {
        _ = sender;
        _ = choice;

        await this.AddReferenceAsync(AssetKind.PhysicsScene, choice, this.AddPhysicsSidecarFlyout).ConfigureAwait(true);
    }

    private async Task AddReferenceAsync(AssetKind kind, AssetChoice choice, Flyout flyout)
    {
        if (choice.Asset is not { } asset || this.ViewModel is not { } model)
        {
            return;
        }

        flyout.Hide();
        await model.AssignAsync(kind, asset).ConfigureAwait(true);
    }

    private async void AddExtraAsset_Click(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        if (this.ViewModel is { } model)
        {
            await model.AddExtraAssetAsync().ConfigureAwait(true);
        }
    }

    private async void RemoveReference_Click(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        if (sender is Button { Tag: SceneReferenceRow row })
        {
            if (this.ViewModel is { } model)
            {
                await model.RemoveAsync(row).ConfigureAwait(true);
            }
        }
    }

    private async void RemoveExtraAsset_Click(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        if (sender is Button { Tag: ExtraAssetRow row } && this.ViewModel is { } model)
        {
            await model.RemoveExtraAssetAsync(row.Asset).ConfigureAwait(true);
        }
    }

    private async void RefreshCatalog_Click(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        if (this.ViewModel is { } model)
        {
            await model.RefreshCatalogAsync().ConfigureAwait(true);
        }
    }
}
