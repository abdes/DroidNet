// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Cooking;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Routes cooking recovery to the existing document and inspector owners.</summary>
public partial class WorkspaceViewModel
{
    private CookingPanelViewModel? cookingPanel;

    /// <inheritdoc />
    public Task<bool> SaveListedAsync(CookRunSnapshot run)
        => this.projectContextService.ActiveProject is { } project && run.ProjectId == project.ProjectId
            && Dockable.FromId(this.CenterOutletName)?.ViewModel is DocumentHostViewModel host
            ? host.SaveDocumentsAsync([.. run.UnsavedDocuments.Select(document => project.FindByFilePath(document.SourcePath)?.AssetUri).OfType<Uri>()])
            : Task.FromResult(false);

    /// <inheritdoc />
    public Task<bool> OpenDocumentAsync(string sourcePath)
        => this.projectContextService.ActiveProject?.FindByFilePath(sourcePath) is { } location
            && Dockable.FromId(this.CenterOutletName)?.ViewModel is DocumentHostViewModel host
            ? host.SelectDocumentAsync(location.AssetUri) : Task.FromResult(false);

    /// <inheritdoc />
    public async Task<bool> GoToPropertyAsync(CookRunSnapshot run, DiagnosticRecord issue)
    {
        if (run.ProjectId != this.projectContextService.ActiveProject?.ProjectId
            || issue.AffectedEntity?.SceneId is not { } sceneId
            || issue.SuggestedAction?.Payload.TryGetValue("PropertyPath", out var property) != true
            || property is null
            || (FindScriptSlotNode(issue, property) is null && !IsSceneProperty(property))
            || this.projectContextService.ActiveProject is not { } project
            || this.assetCatalog is not { } catalog
            || await SceneFiles.FindByIdAsync(catalog, project, sceneId).ConfigureAwait(true) is not { } scene
            || this.documentManager is null
            || issue.AffectedPath is not { } sourcePath || !File.Exists(sourcePath))
        {
            return false;
        }

        var slotNode = FindScriptSlotNode(issue, property);
        if (slotNode is null && Dockable.FromId("se")?.ViewModel is SceneExplorerViewModel explorer)
        {
            explorer.SelectEnvironment(sceneId);
        }

        if (!await this.documentManager.OpenSceneAsync(scene.AssetUri).ConfigureAwait(true)
            || Dockable.FromId("props") is not { ViewModel: SceneNodeEditorViewModel inspector } properties)
        {
            return false;
        }

        // A script slot problem selects its node, whose inspector shows the slot.
        if (slotNode is { } node && Dockable.FromId("se")?.ViewModel is SceneExplorerViewModel nodes)
        {
            _ = await nodes.SetSelectedNodes([node]).ConfigureAwait(true);
        }

        var cooking = Dockable.FromId("cook");
        this.SeparateFromCooking(properties, cooking);
        this.RevealTool(properties);
        if (cooking is not null)
        {
            this.RevealTool(cooking);
        }

        if (slotNode is null)
        {
            inspector.FocusEnvironmentField(sceneId, property);
        }

        return true;
    }

    private static bool IsSceneProperty(string property)
        => string.Equals(property, SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal)
            || Oxygen.Editor.ContentPipeline.SceneStartingPropertyPaths.IsStartingProperty(property);

    private static NodeId? FindScriptSlotNode(DiagnosticRecord issue, string property)
        => Oxygen.Editor.ContentPipeline.ScriptSlotDiagnostics.TryParsePropertyPath(property, out _, out _)
            && issue.SuggestedAction?.Payload.TryGetValue("NodeId", out var text) == true
            && Guid.TryParse(text, System.Globalization.CultureInfo.InvariantCulture, out var node)
                ? NodeId.FromValue(node) : null;

    private void OnCookingRevealRequested(object? sender, EventArgs args)
    {
        if (Dockable.FromId("cook") is { } dockable)
        {
            this.RevealTool(dockable);
        }
    }

    private void RevealTool(Dockable dockable)
    {
        if (dockable.Owner is { } dock)
        {
            this.Docker?.PinDock(dock);
            dockable.IsActive = true;
        }
    }

    // Properties shares a dock with Cooking by default; it moves to its own dock so both stay visible.
    private void SeparateFromCooking(Dockable properties, Dockable? cooking)
    {
        if (properties.Owner is not { } shared || !ReferenceEquals(shared, cooking?.Owner) || this.Docker is not { } docker)
        {
            return;
        }

        shared.DisownDockable(properties);
        var dock = ToolDock.New();
        dock.AdoptDockable(properties);
        var anchor = new Anchor(AnchorPosition.Right);
        try
        {
            docker.Dock(dock, anchor);
            anchor = null;
        }
        finally
        {
            anchor?.Dispose();
        }
    }
}
