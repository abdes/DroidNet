// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Cooking;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Routes cooking recovery to the existing document and inspector owners.</summary>
public partial class WorkspaceViewModel
{
    private CookingPanelViewModel? cookingPanel;

    /// <inheritdoc />
    public Task<bool> SaveListedAsync(CookRunSnapshot run)
        => run.ProjectId == this.projectContextService.ActiveProject?.ProjectId && Dockable.FromId(this.CenterOutletName)?.ViewModel is DocumentHostViewModel host
            ? host.SaveDocumentsAsync(run.UnsavedDocuments.Select(static document => document.DocumentId).ToArray())
            : Task.FromResult(false);

    /// <inheritdoc />
    public Task<bool> OpenDocumentAsync(Guid documentId)
        => Dockable.FromId(this.CenterOutletName)?.ViewModel is DocumentHostViewModel host
            ? host.SelectDocumentAsync(documentId) : Task.FromResult(false);

    /// <inheritdoc />
    public async Task<bool> GoToPropertyAsync(CookRunSnapshot run, DiagnosticRecord issue)
    {
        if (run.ProjectId != this.projectContextService.ActiveProject?.ProjectId
            || issue.AffectedEntity?.SceneId is not { } sceneId
            || issue.SuggestedAction?.Payload.TryGetValue("PropertyPath", out var property) != true
            || property is null
            || !string.Equals(property, SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal)
            || this.projectManager.CurrentProject?.Scenes.FirstOrDefault(scene => scene.Id == sceneId) is not { } scene
            || this.documentManager is null
            || issue.AffectedPath is not { } sourcePath || !File.Exists(sourcePath))
        {
            return false;
        }

        if (Dockable.FromId("se")?.ViewModel is SceneExplorerViewModel explorer)
        {
            explorer.SelectEnvironment(sceneId);
        }

        if (!await this.documentManager.OpenSceneAsync(scene).ConfigureAwait(true)
            || Dockable.FromId("props") is not { ViewModel: SceneNodeEditorViewModel inspector } properties)
        {
            return false;
        }

        var cooking = Dockable.FromId("cook");
        if (properties.Owner is { } shared && ReferenceEquals(shared, cooking?.Owner) && this.Docker is { } docker)
        {
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

        this.RevealTool(properties);
        if (cooking is not null)
        {
            this.RevealTool(cooking);
        }

        inspector.FocusEnvironmentField(sceneId, property);
        return true;
    }

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
}
