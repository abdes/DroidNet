// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Scenes;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
/// Placing instances (scene instances 6.1, 8.6): Quick Add's <em>Model…</em> and <em>Scene…</em> pick a source, and the
/// instance root, named after it, is placed like every Quick Add node and selected.
/// </summary>
public partial class SceneEditorViewModel
{
    private IReadOnlyList<AssetEntry> latestAssets = [];
    private IDisposable? assetSubscription;

    /// <summary>Occurs when Quick Add asks the view to show <see cref="InstanceSourceChoices"/>.</summary>
    public event EventHandler? InstanceSourcePickerRequested;

    /// <summary>Gets the sources Quick Add last offered: the imported models, or the scenes that may be instanced here.</summary>
    public AssetPickerModel? InstanceSourceChoices { get; private set; }

    /// <summary>Gets what the source picker says when it lists nothing.</summary>
    public string InstanceSourceEmptyText { get; private set; } = string.Empty;

    /// <summary>Gets the models Quick Add's <em>Model…</em> offers: one row per imported model's source file.</summary>
    internal AssetPickerModel ModelChoices { get; } = InstanceSources.CreateModelPicker();

    /// <summary>Gets the scenes Quick Add's <em>Scene…</em> offers: authored scenes that would not instance this one.</summary>
    internal AssetPickerModel SceneChoices { get; } = InstanceSources.CreateScenePicker();

    /// <summary>
    /// Places an instance of a model or a scene in front of the focused pane's camera, named after its source, and
    /// selects it. A source that is or instances this scene is refused.
    /// </summary>
    /// <param name="source">The model's source file or the authored scene.</param>
    /// <returns><see langword="true"/> when the instance was placed.</returns>
    internal async Task<bool> AddInstanceAsync(AssetPath source)
    {
        ArgumentNullException.ThrowIfNull(source);
        this.LogAddInstanceRequested(source);
        if (this.Authoring is not { } authoring)
        {
            return false;
        }

        // The edit checks the source against the saved scenes as they are now.
        if (this.services.References is { } references)
        {
            await references.RefreshAsync(CancellationToken.None).ConfigureAwait(true);
        }

        var placement = await this.GetPlacementAsync(alignToView: false).ConfigureAwait(true);
        var node = SceneNodeTemplates.Instance(authoring.Scene, source, placement);
        var result = await authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(authoring.Scene, authoring.Layout, node)])).ConfigureAwait(true);
        if (!result.Succeeded)
        {
            this.PublishInstanceRefused(source, result);
            return false;
        }

        this.SelectionService.Publish(this.Document.Id, SceneSelectionContext.ForNodes([NodeReference.To(node.Id)]), SelectionSource);
        return true;
    }

    private void InitializeInstances()
        => this.assetSubscription = this.services.Assets?.Items.Subscribe(assets => this.latestAssets = assets);

    private void DisposeInstances()
    {
        this.assetSubscription?.Dispose();
        this.assetSubscription = null;
    }

    private void PickModel()
    {
        this.LogQuickAddRequested("Model");
        this.ModelChoices.Update(InstanceSources.Models(this.latestAssets));
        this.RequestInstanceSourcePicker(this.ModelChoices, "No imported models. Import one from the Content Explorer.");
    }

    private async Task PickSceneAsync()
    {
        this.LogQuickAddRequested("Scene");
        if (this.services.References is { } references)
        {
            await references.RefreshAsync(CancellationToken.None).ConfigureAwait(true);
        }

        var scene = this.Document.File.Asset;
        this.SceneChoices.Update(InstanceSources.Scenes(this.latestAssets, source => this.WouldCycle(scene, source)));
        this.RequestInstanceSourcePicker(this.SceneChoices, "No other scene can be instanced here.");
    }

    private bool WouldCycle(AssetPath scene, AssetPath source)
        => InstanceRules.WouldCycle(scene, source, asset => this.services.References?.ReferencesOf(asset) ?? []);

    private void RequestInstanceSourcePicker(AssetPickerModel choices, string emptyText)
    {
        this.InstanceSourceChoices = choices;
        this.InstanceSourceEmptyText = emptyText;
        this.InstanceSourcePickerRequested?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>
    /// Refuses a structural viewport command, Delete or Duplicate, over a selection that holds a node an instance
    /// brought in: the command is refused as a whole and says why (scene instances 6.2).
    /// </summary>
    /// <param name="command">The viewport command.</param>
    /// <returns><see langword="true"/> when the command is refused.</returns>
    private bool RefusesInstancedStructure(ViewportEditCommand command)
    {
        if (this.scene is not { } scene
            || InstanceRules.RefusesStructure(scene, this.SelectionService.GetContext(this.Document.Id).SelectedNodes) is not { } refusal)
        {
            return false;
        }

        this.LogInstancedStructureRefused(command, refusal.Node);
        _ = SceneOperationResults.PublishFailure(
            this.services.OperationResults,
            this.services.StatusReducer,
            command == ViewportEditCommand.Delete ? SceneOperationKinds.NodeDelete : SceneOperationKinds.NodeDuplicate,
            FailureDomain.SceneAuthoring,
            refusal.Code,
            command == ViewportEditCommand.Delete ? "Nodes not deleted" : "Nodes not duplicated",
            refusal.Message,
            this.SceneScope());
        return true;
    }

    private void PublishInstanceRefused(AssetPath source, SceneEditResult result)
    {
        var error = result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError);
        this.LogAddInstanceRefused(source, error?.Code);
        if (error is null)
        {
            return;
        }

        _ = SceneOperationResults.PublishFailure(
            this.services.OperationResults,
            this.services.StatusReducer,
            SceneOperationKinds.NodeCreate,
            FailureDomain.SceneAuthoring,
            error.Code,
            "Instance not placed",
            error.Message,
            this.SceneScope());
    }
}
