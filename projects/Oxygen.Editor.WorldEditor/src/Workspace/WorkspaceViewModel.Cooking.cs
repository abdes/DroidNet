// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Docking;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
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
    private HostingContext? cookHosting;
    private IProjectAssetCatalog? cookedCatalog;
    private IDisposable? publicationRegistration;

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

    private Task<ICookPublicationPreview?> CreatePublicationPreviewAsync(Oxygen.Editor.Projects.ProjectContext project)
        => this.cookHosting!.Dispatcher.DispatchAsync(() => Task.FromResult<ICookPublicationPreview?>(
            this.publicationRegistration is not null && ReferenceEquals(project, this.projectContextService.ActiveProject)
                ? new WorkspacePublicationPreview(this, project) : null));

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

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Workspace disposal observes native teardown failures; the runtime retains readers until safe cleanup.")]
    private async Task ReleaseCookedRootsAsync()
    {
        try
        {
            await this.engineService.RefreshProjectCookedRootsAsync([]).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            this.LogValidatedMountFailed(exception, string.Empty);
        }
    }

    private sealed partial class WorkspacePublicationPreview(WorkspaceViewModel owner, Oxygen.Editor.Projects.ProjectContext project) : ICookPublicationPreview
    {
        public bool IsRuntimeAvailable { get; } = owner.engineService.State == Oxygen.Editor.Runtime.Engine.EngineServiceState.Running;

        private bool IsCurrent => owner.publicationRegistration is not null && ReferenceEquals(project, owner.projectContextService.ActiveProject);

        public Task PrepareReplacementAsync() => owner.cookHosting!.Dispatcher.DispatchAsync(async () =>
        {
            if (this.IsCurrent && this.IsRuntimeAvailable)
            {
                await owner.engineService.SuspendCookedContentAsync().ConfigureAwait(true);
            }
        });

        public Task MountAsync(IReadOnlyList<string> roots, CookOutputWriteLease? writer) => owner.cookHosting!.Dispatcher.DispatchAsync(async () =>
        {
            if (this.IsCurrent && this.IsRuntimeAvailable)
            {
                var reader = writer?.CreateReader() ?? CookOutputLease.AcquireRead(project.ProjectRoot);
                await owner.engineService.RefreshProjectCookedRootsAsync(roots, reader, keepPaused: true).ConfigureAwait(true);
            }
        });

        public Task ResumeAsync() => owner.cookHosting!.Dispatcher.DispatchAsync(async () =>
        {
            if (this.IsCurrent)
            {
                await owner.cookedCatalog!.RefreshAsync(CancellationToken.None).ConfigureAwait(true);
                _ = owner.messenger?.Send(new AssetsChangedMessage());
                if (this.IsRuntimeAvailable)
                {
                    await owner.engineService.ResumeCookedContentAsync().ConfigureAwait(true);
                }
            }
        });

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
