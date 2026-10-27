// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Projects;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>
/// ViewModel that provides editing support for the <see cref="GeometryComponent"/> of one or
/// more selected <see cref="SceneNode"/> instances.
/// </summary>
/// <remarks>
/// The view model exposes a list of available geometry asset groups and routes edits through
/// the scene document command service so undo, dirty state, diagnostics, and live sync stay
/// on the document command path.
/// </remarks>
public sealed partial class GeometryViewModel : ComponentPropertyEditor, IDisposable
{
    private readonly IAssetFeed assetProvider;
    private readonly IGeometrySurfaceProvider surfaces;
    private readonly IProjectSession session;
    private readonly Func<SceneAuthoring?>? authoringProvider;
    private readonly PropertyBinding<AssetPath?> geometryUriBinding = new(SceneProperties.Geometry.GeometryUriDescriptor);
    private readonly DispatcherQueue dispatcherQueue;
    private readonly ILogger logger;

    private CancellationTokenSource? assetChangesSubscription;
    private CancellationTokenSource? materialResultsSubscription;
    private IReadOnlyList<AssetEntry> latestMaterialAssets = [];

    private ICollection<SceneNode>? selectedItems;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="GeometryViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The WinUI hosting context used for UI-thread dispatch.</param>
    /// <param name="assetProvider">The shared asset identity, cook status and runtime availability feed.</param>
    /// <param name="surfaces">The native geometry surface inventory reader.</param>
    /// <param name="session">The project session this panel belongs to.</param>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    /// <param name="loggerFactory">Creates the logger that reports asset feed failures.</param>
    public GeometryViewModel(
        HostingContext hosting,
        IAssetFeed assetProvider,
        IGeometrySurfaceProvider surfaces,
        IProjectSession session,
        Func<SceneAuthoring?>? authoringProvider = null,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<GeometryViewModel>() ?? NullLogger<GeometryViewModel>.Instance;
        this.assetProvider = assetProvider;
        this.surfaces = surfaces;
        this.session = session;
        this.authoringProvider = authoringProvider;
        this.dispatcherQueue = hosting.Dispatcher;
        this.LevelOfDetail = new(this);

        this.StartMaterialPickerSubscription();
        this.StartAssetCatalogSubscription();
    }

    /// <summary>Gets the geometries the picker offers, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel GeometryChoices { get; } = new(static asset => AssetChoice.For(asset, "Geometry", "\uF158"));

    /// <summary>Gets the materials a surface's picker offers.</summary>
    public AssetPickerModel MaterialChoices { get; } = new(static asset => AssetChoice.For(asset, "Material", "\uE8B9"));

    /// <inheritdoc />
    public override string Header => "Geometry";

    /// <inheritdoc />
    public override string Description => "References a geometry asset and overrides its surfaces.";

    /// <summary>
    /// Gets or sets a value indicating whether the currently selected scene nodes have
    /// differing geometry assets. When <see langword="true"/>, the UI should indicate a mixed state.
    /// </summary>
    [ObservableProperty]
    public partial bool IsMixed { get; set; }

    /// <summary>
    /// Gets or sets the currently selected geometry asset. When multiple nodes are selected and
    /// they reference different assets this value will be <see langword="null"/>.
    /// </summary>
    [ObservableProperty]
    public partial AssetPath? SelectedGeometry { get; set; }

    /// <summary>
    /// Gets or sets the display name of the currently selected geometry asset. When no asset
    /// is selected the value will be "None" and when multiple different assets are selected
    /// the value will be "--".
    /// </summary>
    [ObservableProperty]
    public partial string SelectedAssetName { get; set; } = "None";

    /// <summary>
    /// Gets a value indicating whether the selection's geometry asset can be replaced: no selected node is one an instance
    /// brings in, whose geometry its source decides.
    /// </summary>
    [ObservableProperty]
    public partial bool CanChangeGeometry { get; private set; } = true;

    /// <summary>
    /// Gets the label to show on the asset button. Returns "--" when <see cref="IsMixed"/>,
    /// otherwise returns the <see cref="SelectedAssetName"/>.
    /// </summary>
    public string AssetButtonLabel => this.IsMixed ? "--" : this.SelectedAssetName;

    /// <summary>
    /// Gets a glyph string to use as a thumbnail indicator for the currently selected asset.
    /// Returns an indeterminate glyph when <see cref="IsMixed"/>, an empty string when no
    /// asset is selected, or a geometry glyph for a valid selection.
    /// </summary>
    public string AssetThumbnailGlyph
    {
        get
        {
            if (this.IsMixed)
            {
                return "\uE9D9"; // Indeterminate / mixed
            }

            if (this.SelectedGeometry is null)
            {
                return string.Empty; // Empty placeholder
            }

            return "\uF158"; // Geometry
        }
    }

    /// <summary>
    /// Apply the specified geometry asset to all currently selected scene nodes.
    /// </summary>
    /// <param name="choice">The picked geometry. A disabled choice, or one without an asset, is ignored.</param>
    /// <returns>A <see cref="Task"/> that completes when the operation has finished.</returns>
    public async Task ApplyAssetAsync(AssetChoice choice)
    {
        if (!this.IsInputEnabled || !this.CanChangeGeometry || choice is not { IsEnabled: true, Asset: { } geometry })
        {
            return;
        }

        if (this.selectedItems is null || this.selectedItems.Count == 0)
        {
            return;
        }

        var nodes = this.selectedItems.ToList();
        if (this.authoringProvider?.Invoke() is not { } authoring)
        {
            return;
        }

        var result = await authoring.ExecuteAsync(new SetGeometry(nodes.ConvertAll(static node => node.Id), geometry)).ConfigureAwait(true);
        if (!result.Succeeded || !this.IsCurrentSelection(nodes))
        {
            return;
        }

        // Update viewmodel display
        this.SelectedGeometry = geometry;
        this.SelectedAssetName = choice.Name;
        this.IsMixed = false;
        await this.RefreshSurfacesAsync().ConfigureAwait(true);
    }

    /// <summary>
    /// Update the view model state from the given collection of selected scene nodes.
    /// </summary>
    /// <param name="items">The currently selected scene nodes.</param>
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.selectedItems = items;

        var targets = items
            .Select(static node => new { Node = node, Geometry = node.Components.OfType<GeometryComponent>().FirstOrDefault() })
            .Where(static target => target.Geometry is not null)
            .ToList();
        var references = targets.ConvertAll(target => this.ReferenceOf(target.Node));
        var targetsByNode = targets.ToDictionary(target => this.ReferenceOf(target.Node), static target => (object?)target.Geometry);

        this.geometryUriBinding.UpdateFromModel(references, reference => targetsByNode.TryGetValue(reference, out var target) ? target : null);
        this.CanChangeGeometry = references.TrueForAll(static reference => reference.IsOwned);
        this.IsMixed = this.geometryUriBinding.IsMixed;

        if (this.geometryUriBinding.IsMixed)
        {
            this.SelectedGeometry = null;
            this.SelectedAssetName = "--";
        }
        else
        {
            var selected = this.geometryUriBinding.HasValue ? this.geometryUriBinding.Value : null;
            this.SelectedGeometry = selected;
            this.SelectedAssetName = selected is null ? "None" : this.ResolveGeometryDisplayName(selected);
        }

        // Authored overrides change synchronously, including during undo/redo, and the cards show them in place. Only
        // another selection or geometry needs the inventory read again: an override never changes it.
        this.ProjectSurfaces();
        if (!this.ShowsInventoryOf(items))
        {
            _ = this.RefreshSurfacesAsync();
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.disposed = true;
        this.surfaceRefresh?.Cancel();
        this.surfaceRefresh?.Dispose();
        LifetimeSubscription.End(ref this.assetChangesSubscription);
        LifetimeSubscription.End(ref this.materialResultsSubscription);
        GC.SuppressFinalize(this);
    }

    /// <summary>Gets the user-facing name of an asset: its name without its file type.</summary>
    /// <param name="asset">The asset.</param>
    /// <returns>The user-facing name.</returns>
    internal static string NameOf(AssetPath asset) => AssetFileTypes.RemoveSuffix(asset.Name);

    private bool IsCurrentSelection(List<SceneNode> nodes)
        => !this.disposed && this.selectedItems?.Count == nodes.Count
            && nodes.TrueForAll(node => this.selectedItems.Any(selected => ReferenceEquals(selected, node)));

    private void StartMaterialPickerSubscription()
        => this.materialResultsSubscription = LifetimeSubscription.Subscribe(
            this.assetProvider.Items,
            assets => this.DispatchOnUi(() =>
            {
                this.latestMaterialAssets = assets;
                this.ApplyContentMaterials();
            }),
            this.LogAssetFeedFailed,
            this.session.Lifetime);

    // Lists the materials the surface accepts, keeping the one assigned now even when it is gone.
    private void ApplyContentMaterials()
        => this.MaterialChoices.Update(AssetRule.For(AssetKind.Material).Apply(this.latestMaterialAssets, this.pickedMaterial));

    private string ResolveMaterialDisplayName(AssetPath material)
        => this.MaterialChoices.Find(material)?.Name ?? NameOf(material);

    // Use the shared WinUI dispatcher helpers to handle shutdown / exceptions robustly.
    private void DispatchOnUi(Action action)
        => _ = this.dispatcherQueue.DispatchAsync(action)
            .ContinueWith(
                t =>
                {
                    if (t.IsFaulted)
                    {
                        this.LogDispatchFailed(t.Exception!);
                    }
                },
                CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);

    partial void OnIsMixedChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.AssetButtonLabel));
        this.OnPropertyChanged(nameof(this.AssetThumbnailGlyph));
    }

    partial void OnSelectedAssetNameChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.AssetButtonLabel));
    }

    partial void OnSelectedGeometryChanged(AssetPath? value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.AssetThumbnailGlyph));
    }
}
