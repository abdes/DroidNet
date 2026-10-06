// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentBrowser.AssetIdentity;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Composes typed asset pickers and retained scene-reference rows.</summary>
[ViewModel(typeof(SceneReferencesSectionViewModel))]
public sealed partial class SceneReferencesSectionView
{
    /// <summary>Initializes a new instance of the <see cref="SceneReferencesSectionView"/> class.</summary>
    public SceneReferencesSectionView() => this.InitializeComponent();

    private async void AddScript_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.AssignAsync(AssetKind.Script);
        }
    }

    private async void AddInputAction_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.AssignAsync(AssetKind.InputAction);
        }
    }

    private async void AddInputMappingContext_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.AssignAsync(AssetKind.InputMappingContext);
        }
    }

    private async void AddPhysicsSidecar_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.AssignAsync(AssetKind.PhysicsScene);
        }
    }

    private async void AddExtraAsset_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.AddExtraAssetAsync();
        }
    }

    private async void RemoveReference_Click(object sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: SceneReferenceRow row })
        {
            if (this.ViewModel is { } model)
            {
                await model.RemoveAsync(row);
            }
        }
    }

    private async void RemoveExtraAsset_Click(object sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: ExtraAssetRow row } && this.ViewModel is { } model)
        {
            await model.RemoveExtraAssetAsync(row.Path);
        }
    }

    private async void RefreshCatalog_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.RefreshCatalogAsync();
        }
    }
}
