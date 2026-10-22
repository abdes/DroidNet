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
///     The scene policies of a window's documents, over the window's document area: the project remembers the scene
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
    private readonly IDocumentArea area;
    private readonly IProjectSession session;
    private readonly IProjectUsageService projectUsage;
    private (Guid ProjectId, FileSystemPath ProjectRoot, AssetPath Scene)? previousScene;
    private DocumentId? lastActivated;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="SceneDocumentLifetime"/> class.</summary>
    /// <param name="area">The window's document area, which opens and places its documents.</param>
    /// <param name="session">The project session the window belongs to.</param>
    /// <param name="projectUsage">The project's usage record, which keeps its last scene.</param>
    /// <param name="loggerFactory">The logger factory, if any.</param>
    public SceneDocumentLifetime(
        IDocumentArea area,
        IProjectSession session,
        IProjectUsageService projectUsage,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<SceneDocumentLifetime>() ?? NullLoggerFactory.Instance.CreateLogger<SceneDocumentLifetime>();
        this.area = area;
        this.session = session;
        this.projectUsage = projectUsage;

        this.area.Placed += this.OnPlaced;
        this.area.ActiveDocumentChanged += this.OnActiveDocumentChanged;
    }

    /// <inheritdoc/>
    public event EventHandler<SceneUnavailableEventArgs>? SceneUnavailable;

    /// <inheritdoc/>
    public async Task<bool> ReloadPreviousAsync()
        => this.previousScene is { } previous
            && this.session.Context.ProjectId == previous.ProjectId
            && this.session.Context.ProjectRoot == previous.ProjectRoot
            && (await this.area.OpenAsync(previous.Scene).ConfigureAwait(true)).Succeeded;

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.area.Placed -= this.OnPlaced;
        this.area.ActiveDocumentChanged -= this.OnActiveDocumentChanged;
    }

    // Scenes are the only documents that replace others, so a placement that replaced documents replaced scenes.
    private void OnPlaced(object? sender, DocumentPlacedEventArgs args)
    {
        var placement = args.Placement;
        if (placement.Replaced.Count == 0)
        {
            return;
        }

        var project = this.session.Context;
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
        var project = this.session.Context;
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
