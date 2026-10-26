// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Menus;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
/// The scene document toolbar: Quick Add, which creates nodes in front of the focused pane's
/// camera; Browse to Asset, which reveals the assets the selected nodes use; and Environment,
/// which selects the scene for the Inspector.
/// </summary>
public partial class SceneEditorViewModel
{
    private const string SelectionSource = "SceneToolbar";

    private static readonly (PrimitiveKind Kind, string Label)[] PrimitiveKinds =
    [
        (PrimitiveKind.Sphere, "Sphere"),
        (PrimitiveKind.Cube, "Cube"),
        (PrimitiveKind.Cylinder, "Cylinder"),
        (PrimitiveKind.Cone, "Cone"),
        (PrimitiveKind.Plane, "Plane"),
        (PrimitiveKind.Capsule, "Capsule"),
        (PrimitiveKind.IcoSphere, "Icosphere"),
        (PrimitiveKind.Torus, "Torus"),
        (PrimitiveKind.Quad, "Quad"),
        (PrimitiveKind.SubdividedCube, "Subdivided cube"),
    ];

    private IReadOnlyList<SceneNode> selectedNodes = [];
    private IMenuSource? quickAddMenu;

    /// <summary>
    /// Gets the Quick Add menu: an empty node, the built-in shapes, lights, cameras and fog volumes,
    /// and instances of a model or a scene, each created in front of the focused pane's camera.
    /// Instances are selected once placed.
    /// </summary>
    public IMenuSource QuickAddMenu => this.quickAddMenu ??= this.BuildQuickAddMenu();

    private IMenuSource BuildQuickAddMenu()
    {
        var builder = new MenuBuilder(this.services.LoggerFactory);
        _ = builder.AddMenuItem("Empty node", new AsyncRelayCommand(this.AddEmptyNodeAsync));
        _ = builder.AddSeparator();
        _ = builder.AddSubmenu("Shapes", shapes =>
        {
            foreach (var (kind, label) in PrimitiveKinds)
            {
                _ = shapes.AddMenuItem(label, new AsyncRelayCommand(() => this.AddPrimitiveAsync(kind)));
            }
        });
        _ = builder.AddSubmenu("Lights", lights =>
        {
            _ = lights.AddMenuItem("Directional light", new AsyncRelayCommand(() => this.AddLightAsync(LightKind.Directional)));
            _ = lights.AddMenuItem("Point light", new AsyncRelayCommand(() => this.AddLightAsync(LightKind.Point)));
            _ = lights.AddMenuItem("Spot light", new AsyncRelayCommand(() => this.AddLightAsync(LightKind.Spot)));
        });
        _ = builder.AddSubmenu("Cameras", cameras =>
        {
            _ = cameras.AddMenuItem("Perspective camera", new AsyncRelayCommand(() => this.AddCameraAsync(CameraKind.Perspective)));
            _ = cameras.AddMenuItem("Orthographic camera", new AsyncRelayCommand(() => this.AddCameraAsync(CameraKind.Orthographic)));
        });
        _ = builder.AddMenuItem("Local fog volume", new AsyncRelayCommand(this.AddLocalFogVolumeAsync));
        _ = builder.AddSeparator();
        _ = builder.AddMenuItem("Model…", new RelayCommand(this.PickModel));
        _ = builder.AddMenuItem("Scene…", new AsyncRelayCommand(this.PickSceneAsync));
        return builder.Build();
    }

    private async Task AddEmptyNodeAsync()
    {
        this.LogQuickAddRequested("Empty");
        var placement = await this.GetPlacementAsync(alignToView: false).ConfigureAwait(true);
        await this.InsertAsync(scene => SceneNodeTemplates.Empty(scene, placement: placement)).ConfigureAwait(true);
    }

    private async Task AddPrimitiveAsync(PrimitiveKind kind)
    {
        this.LogQuickAddRequested(kind.ToString());
        var placement = await this.GetPlacementAsync(alignToView: false).ConfigureAwait(true);
        await this.InsertAsync(scene => SceneNodeTemplates.Primitive(scene, kind, placement)).ConfigureAwait(true);
    }

    private async Task AddLightAsync(LightKind kind)
    {
        this.LogQuickAddRequested(kind + " light");

        // A light keeps its kind's default orientation; only its position follows the view.
        var placement = await this.GetPlacementAsync(alignToView: false).ConfigureAwait(true);
        await this.InsertAsync(scene => SceneNodeTemplates.Light(scene, kind, placement)).ConfigureAwait(true);
    }

    private async Task AddLocalFogVolumeAsync()
    {
        this.LogQuickAddRequested("Local fog volume");
        var placement = await this.GetPlacementAsync(alignToView: false).ConfigureAwait(true);
        await this.InsertAsync(scene => SceneNodeTemplates.LocalFogVolume(scene, placement)).ConfigureAwait(true);
    }

    private async Task AddCameraAsync(CameraKind kind)
    {
        this.LogQuickAddRequested(kind + " camera");

        // A new camera sees what the focused pane sees.
        var placement = await this.GetPlacementAsync(alignToView: true).ConfigureAwait(true);
        await this.InsertAsync(scene => SceneNodeTemplates.Camera(scene, kind, placement)).ConfigureAwait(true);
    }

    // Quick Add appends a root node: last among the root nodes and in the explorer's root.
    private async Task InsertAsync(Func<Scene, SceneNodeData> template)
    {
        if (this.Authoring is { } authoring)
        {
            var node = template(authoring.Scene);
            _ = await authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(authoring.Scene, authoring.Layout, node)])).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Where Quick Add creates a node: at the focused pane's orbit focus, the point in front of its
    /// camera, or at the camera itself with its rotation. <see langword="null"/> (the scene origin)
    /// when no pane has reported its camera.
    /// </summary>
    private async Task<NodePlacement?> GetPlacementAsync(bool alignToView)
    {
        if (this.GetActiveViewport() is not { } viewport)
        {
            return null;
        }

        await viewport.RefreshEditorCameraAsync().ConfigureAwait(true);
        return viewport.EditorCamera is not { } camera
            ? null
            : alignToView
                ? new NodePlacement(camera.Position, camera.Rotation)
                : new NodePlacement(camera.FocusPoint);
    }

    private void UpdateSelectedNodes(IReadOnlyList<SceneNode> nodes)
    {
        this.selectedNodes = [.. nodes];
        this.BrowseToAssetCommand.NotifyCanExecuteChanged();
    }

    private IReadOnlyList<AssetPath> GetSelectedAssets()
        => [.. this.selectedNodes.SelectMany(SceneAssetReferences.Runtime).Distinct()];

    private bool CanBrowseToAsset() => this.GetSelectedAssets().Count > 0;

    /// <summary>Reveals and selects the selected nodes' geometry and material assets in the Content Explorer (Ctrl+B).</summary>
    [RelayCommand(CanExecute = nameof(CanBrowseToAsset))]
    private async Task BrowseToAsset()
    {
        var assets = this.GetSelectedAssets();
        this.LogBrowseToAssetRequested(assets.Count);
        if (assets.Count == 0 || this.window?.Reveal is not { } reveal)
        {
            return;
        }

        _ = await reveal.RevealAsync(assets).ConfigureAwait(true);
    }

    /// <summary>Selects the scene itself, so the Inspector shows its environment.</summary>
    [RelayCommand]
    private void SelectEnvironment()
        => this.SelectionService.Publish(
            this.Document.Id,
            new SceneSelectionContext(SceneSelectionKind.Scene, [], [], PrimaryNode: null, PrimaryFolderId: null),
            SelectionSource);
}
