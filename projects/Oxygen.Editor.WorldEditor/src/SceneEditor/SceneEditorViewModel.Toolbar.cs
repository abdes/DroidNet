// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls.Menus;
using DryIoc;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// The scene document toolbar: Quick Add, which creates nodes in front of the focused pane's
/// camera; Browse to Asset, which reveals the assets the selected nodes use; and Environment,
/// which selects the scene for the Inspector.
/// </summary>
public partial class SceneEditorViewModel
{
    private const string SelectionSource = "SceneToolbar";

    private static readonly (string Kind, string Label)[] PrimitiveKinds =
    [
        ("Sphere", "Sphere"),
        ("Cube", "Cube"),
        ("Cylinder", "Cylinder"),
        ("Cone", "Cone"),
        ("Plane", "Plane"),
        ("Capsule", "Capsule"),
        ("IcoSphere", "Icosphere"),
        ("Torus", "Torus"),
        ("Quad", "Quad"),
        ("SubdividedCube", "Subdivided cube"),
    ];

    private IReadOnlyList<SceneNode> selectedNodes = [];
    private IMenuSource? quickAddMenu;

    /// <summary>
    /// Gets the Quick Add menu: an empty node, the built-in shapes, lights and cameras, each created
    /// in front of the focused pane's camera and selected.
    /// </summary>
    public IMenuSource QuickAddMenu => this.quickAddMenu ??= this.BuildQuickAddMenu();

    private static bool IsAuthoredAsset(Uri? uri)
        => uri is not null && !uri.AbsolutePath.EndsWith("/__uninitialized__", StringComparison.Ordinal);

    /// <summary>The geometry and material assets a node uses, in a stable order.</summary>
    private static IEnumerable<Uri> GetAssetUris(SceneNode node)
    {
        var geometry = node.Components.OfType<GeometryComponent>().FirstOrDefault();
        if (geometry?.Geometry?.Uri is { } geometryUri)
        {
            yield return geometryUri;
        }

        var slots = node.OverrideSlots
            .Concat(geometry?.OverrideSlots ?? [])
            .Concat(geometry?.TargetedOverrides.SelectMany(target => target.OverrideSlots) ?? []);
        foreach (var materials in slots.OfType<MaterialsSlot>())
        {
            yield return materials.Material.Uri;
        }
    }

    private IMenuSource BuildQuickAddMenu()
    {
        var builder = new MenuBuilder(this.loggerFactory);
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
            _ = lights.AddMenuItem("Directional light", new AsyncRelayCommand(() => this.AddLightAsync("Directional")));
            _ = lights.AddMenuItem("Point light", new AsyncRelayCommand(() => this.AddLightAsync("Point")));
            _ = lights.AddMenuItem("Spot light", new AsyncRelayCommand(() => this.AddLightAsync("Spot")));
        });
        _ = builder.AddSubmenu("Cameras", cameras =>
        {
            _ = cameras.AddMenuItem("Perspective camera", new AsyncRelayCommand(() => this.AddCameraAsync("Perspective")));
            _ = cameras.AddMenuItem("Orthographic camera", new AsyncRelayCommand(() => this.AddCameraAsync("Orthographic")));
        });
        return builder.Build();
    }

    private async Task AddEmptyNodeAsync()
    {
        this.LogQuickAddRequested("Empty");
        if (this.scene is not null)
        {
            var placement = await this.GetPlacementAsync(alignToView: false).ConfigureAwait(true);
            _ = await this.commandService.CreateEmptyNodeAsync(this.CreateCommandContext(), placement).ConfigureAwait(true);
        }
    }

    private async Task AddPrimitiveAsync(string kind)
    {
        this.LogQuickAddRequested(kind);
        if (this.scene is not null)
        {
            var placement = await this.GetPlacementAsync(alignToView: false).ConfigureAwait(true);
            _ = await this.commandService.CreatePrimitiveAsync(this.CreateCommandContext(), kind, placement).ConfigureAwait(true);
        }
    }

    private async Task AddLightAsync(string kind)
    {
        this.LogQuickAddRequested(kind + " light");
        if (this.scene is not null)
        {
            // A light keeps its kind's default orientation; only its position follows the view.
            var placement = await this.GetPlacementAsync(alignToView: false).ConfigureAwait(true);
            _ = await this.commandService.CreateLightAsync(this.CreateCommandContext(), kind, placement).ConfigureAwait(true);
        }
    }

    private async Task AddCameraAsync(string kind)
    {
        this.LogQuickAddRequested(kind + " camera");
        if (this.scene is not null)
        {
            // A new camera sees what the focused pane sees.
            var placement = await this.GetPlacementAsync(alignToView: true).ConfigureAwait(true);
            _ = await this.commandService.CreateCameraAsync(this.CreateCommandContext(), kind, placement).ConfigureAwait(true);
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

    private void UpdateSelectedNodes(IList<SceneNode> nodes)
    {
        this.selectedNodes = [.. nodes];
        this.BrowseToAssetCommand.NotifyCanExecuteChanged();
    }

    private IReadOnlyList<Uri> GetSelectedAssetUris()
        => [.. this.selectedNodes.SelectMany(GetAssetUris).Where(IsAuthoredAsset).Distinct()];

    private bool CanBrowseToAsset() => this.GetSelectedAssetUris().Count > 0;

    /// <summary>Reveals and selects the selected nodes' geometry and material assets in the Content Browser (Ctrl+B).</summary>
    [RelayCommand(CanExecute = nameof(CanBrowseToAsset))]
    private async Task BrowseToAsset()
    {
        var uris = this.GetSelectedAssetUris();
        this.LogBrowseToAssetRequested(uris.Count);
        if (uris.Count == 0 || this.ActiveProject is not { } project)
        {
            return;
        }

        var request = this.messenger.Send(new ShowAssetRequestMessage(project, uris));
        if (request.HasReceivedResponse)
        {
            _ = await request.Response.ConfigureAwait(true);
        }
    }

    /// <summary>Selects the scene itself, so the Inspector shows its environment.</summary>
    [RelayCommand]
    private void SelectEnvironment()
        => this.container.Resolve<ISceneSelectionService>(IfUnresolved.ReturnDefault)?.Publish(
            this.Metadata.DocumentId,
            new SceneSelectionContext(SceneSelectionKind.Scene, [], [], PrimaryNodeId: null, PrimaryFolderId: null),
            SelectionSource);
}
