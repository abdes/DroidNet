// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Inspector.Geometry;

/// <summary>Projects engine-owned authoring choices and catalog availability into the typed pickers.</summary>
public sealed partial class GeometryViewModel
{
    /// <summary>Gets or sets the availability notice for engine choices.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasBuiltinCatalogNotice))]
    public partial string? BuiltinCatalogNotice { get; set; }

    /// <summary>Gets a value indicating whether engine discovery needs attention.</summary>
    public bool HasBuiltinCatalogNotice => !string.IsNullOrEmpty(this.BuiltinCatalogNotice);

    private async Task InitializeBuiltinsAsync()
    {
        try
        {
            _ = await this.builtins.GetAsync(CancellationToken.None).ConfigureAwait(false);
            this.OnBuiltinCatalogChanged(this, EventArgs.Empty);
        }
        catch (OperationCanceledException)
        {
            // Closing the workspace cancels its discovery source.
        }
    }

    [RelayCommand]
    private async Task RetryBuiltinCatalogAsync()
    {
        _ = await this.builtins.RefreshAsync(CancellationToken.None).ConfigureAwait(true);
        this.ApplyBuiltinCatalog();
    }

    private void OnBuiltinCatalogChanged(object? sender, EventArgs args)
        => this.DispatchOnUi(this.ApplyBuiltinCatalog);

    private void ApplyBuiltinCatalog()
    {
        if (this.disposed)
        {
            return;
        }

        var snapshot = this.builtins.Snapshot;
        this.BuiltinCatalogNotice = snapshot.Notice;
        var geometries = snapshot.Catalog?.AuthoringGeometries.Select(definition =>
        {
            var category = definition.AuthoringCategory == GeneratedAssetCategory.Advanced ? " · Advanced" : string.Empty;
            var availability = snapshot.IsLastKnown ? " · Preview unavailable" : this.BuiltinAvailability(definition.AssetUri);
            return CreateEngineItem(definition.Name, definition.AssetUri, definition.AssetUri.AbsolutePath)
                with { DisplayType = "Built-in geometry" + category + availability, };
        }).ToArray() ?? [];
        for (var index = 0; index < geometries.Length; index++)
        {
            if (index >= this.engineItems.Count)
            {
                this.engineItems.Add(new(geometries[index]));
            }
            else if (this.engineItems[index].Item.Uri == geometries[index].Uri)
            {
                this.engineItems[index].Update(geometries[index]);
            }
            else
            {
                this.engineItems[index] = new(geometries[index]);
            }
        }

        while (this.engineItems.Count > geometries.Length)
        {
            this.engineItems.RemoveAt(this.engineItems.Count - 1);
        }

        if (snapshot.Catalog is null)
        {
            this.engineMaterials.Clear();
        }
        else
        {
            var material = CreateEngineMaterialItem("Default", AssetUris.BuildGeneratedUri("Materials/Default"), "/Engine/Generated/Materials/Default")
                with { DisplayType = snapshot.IsLastKnown ? "Built-in material · Preview unavailable" : "Built-in material" + this.BuiltinAvailability(AssetUris.BuildGeneratedUri("Materials/Default")), };
            if (this.engineMaterials.Count == 0)
            {
                this.engineMaterials.Add(new(material));
            }
            else
            {
                this.engineMaterials[0].Update(material);
            }
        }
    }
}
