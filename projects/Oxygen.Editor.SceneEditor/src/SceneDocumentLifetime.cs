// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
///     The scene policies of a window's documents, over the Shell's document service: the project remembers the scene
///     the window last showed, and a scene that failed to replace the one it closed leaves the window without a scene,
///     from which the previous one can be opened again.
/// </summary>
/// <remarks>
///     The window shows one scene at a time: the scene editor's descriptor allows one document per area, so opening a
///     scene replaces the shown one.
/// </remarks>
public sealed partial class SceneDocumentLifetime : ISceneAvailability, IDisposable
{
    private readonly ILogger logger;
    private readonly IDocumentService documents;
    private readonly IDocumentArea area;
    private readonly IProjectContextService projectContextService;
    private readonly IProjectUsageService projectUsage;
    private (Guid ProjectId, FileSystemPath ProjectRoot, AssetPath Scene)? previousScene;
    private DocumentId? lastActivated;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="SceneDocumentLifetime"/> class.</summary>
    /// <param name="documents">The window's document service.</param>
    /// <param name="area">The window's document area.</param>
    /// <param name="projectContextService">The active project.</param>
    /// <param name="projectUsage">The project's usage record, which keeps its last scene.</param>
    /// <param name="loggerFactory">The logger factory, if any.</param>
    public SceneDocumentLifetime(
        IDocumentService documents,
        IDocumentArea area,
        IProjectContextService projectContextService,
        IProjectUsageService projectUsage,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<SceneDocumentLifetime>() ?? NullLoggerFactory.Instance.CreateLogger<SceneDocumentLifetime>();
        this.documents = documents;
        this.area = area;
        this.projectContextService = projectContextService;
        this.projectUsage = projectUsage;

        this.documents.Placed += this.OnPlaced;
        this.area.ActiveDocumentChanged += this.OnActiveDocumentChanged;
    }

    /// <inheritdoc/>
    public event EventHandler<SceneUnavailableEventArgs>? SceneUnavailable;

    /// <inheritdoc/>
    public async Task<bool> ReloadPreviousAsync()
        => this.previousScene is { } previous
            && this.projectContextService.ActiveProject is { } project
            && project.ProjectId == previous.ProjectId
            && project.ProjectRoot == previous.ProjectRoot
            && (await this.documents.OpenAsync(previous.Scene).ConfigureAwait(true)).Succeeded;

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.documents.Placed -= this.OnPlaced;
        this.area.ActiveDocumentChanged -= this.OnActiveDocumentChanged;
    }

    // Scenes are the only documents that replace others, so a placement that replaced documents replaced scenes.
    private void OnPlaced(object? sender, DocumentPlacedEventArgs args)
    {
        var placement = args.Placement;
        if (placement.Replaced.Count == 0 || this.projectContextService.ActiveProject is not { } project)
        {
            return;
        }

        this.previousScene = (project.ProjectId, project.ProjectRoot, placement.Replaced[^1]);
        if (!placement.Succeeded)
        {
            this.SceneUnavailable?.Invoke(this, new SceneUnavailableEventArgs(canReloadPrevious: true));
        }
    }

    private void OnActiveDocumentChanged(object? sender, EventArgs args)
    {
        if (this.area.ActiveDocument is { Content: SceneContent content } document && document.Id != this.lastActivated)
        {
            this.lastActivated = document.Id;
            _ = this.MarkSceneActivatedAsync(content);
        }
    }

    // The project remembers its last scene by identity: a name is unique only within its folder, and a rename keeps the
    // identity. The workspace still finds scenes remembered by name before.
    private async Task MarkSceneActivatedAsync(SceneContent content)
    {
        if (this.projectContextService.ActiveProject is not { } project
            || content.Project.ProjectId != project.ProjectId
            || content.Project.ProjectRoot != project.ProjectRoot)
        {
            return;
        }

        try
        {
            await this.projectUsage.UpdateLastOpenedSceneAsync(project.Name, project.ProjectRoot.FullPath, content.Scene.Id.ToString()).ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.LogSceneUsageUpdateFailed(ex, content.Scene.Name, project.Name);
        }
    }
}
