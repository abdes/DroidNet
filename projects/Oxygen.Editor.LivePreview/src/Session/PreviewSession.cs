// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>The preview of the open project: the running engine, the project's mounted content and its publication.</summary>
/// <remarks>
/// <para>
/// Starting the session registers the publication preview, starts the engine with its startup settings if it does not
/// run, and mounts the project's cooked content. A failure leaves authoring working: it shows as
/// <see cref="State"/> and <see cref="Failure"/>, is published as an operation result, and <see cref="RetryAsync"/>
/// retries the step that failed.
/// </para>
/// <para>
/// The session raises its property changes on the thread that created it. Disposing it releases the project's cooked
/// content; the engine keeps running for the next project.
/// </para>
/// </remarks>
/// <param name="engine">The embedded engine, shared by every window.</param>
/// <param name="publication">The project's cook publication.</param>
/// <param name="mounts">Prepares a publication for mounting.</param>
/// <param name="catalog">The asset catalog that shows the mounted publication.</param>
/// <param name="coordinator">Serializes project changes with cooking.</param>
/// <param name="projects">The active project.</param>
/// <param name="projectManager">The loaded project, whose mounts follow an accepted mount change.</param>
/// <param name="results">Publishes the session's failures.</param>
/// <param name="status">Reduces the published failures.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class PreviewSession(
    IEngineService engine,
    CookPublicationService publication,
    CookedContentMountService mounts,
    IAssetPublicationTarget catalog,
    IContentCookCoordinator coordinator,
    IProjectContextService projects,
    IProjectManagerService projectManager,
    IOperationResultPublisher results,
    IStatusReducer status,
    ILoggerFactory? loggerFactory = null) : ObservableObject, IContentMountChanges, IAsyncDisposable
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<PreviewSession>() ?? NullLogger<PreviewSession>.Instance;
    private readonly OwnerThread owner = OwnerThread.Capture();
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "A start under way at disposal still releases it; it never allocates a wait handle.")]
    private readonly SemaphoreSlim startGate = new(1, 1);
    private ProjectContext? project;
    private IDisposable? publicationRegistration;
    private bool disposed;

    /// <summary>Gets where the preview stands.</summary>
    [ObservableProperty]
    public partial PreviewSessionState State { get; private set; }

    /// <summary>Gets why the preview is unavailable, or <see langword="null"/> when it is not.</summary>
    [ObservableProperty]
    public partial PreviewFailure? Failure { get; private set; }

    /// <summary>Starts the preview of a project: the publication preview, the engine, then the project's content.</summary>
    /// <param name="project">The project.</param>
    /// <param name="cancellationToken">Cancels waiting for a start already under way.</param>
    /// <returns><see langword="true"/> when the engine runs and shows the project's content.</returns>
    public async Task<bool> StartAsync(ProjectContext project, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        ObjectDisposedException.ThrowIf(this.disposed, this);
        this.project = project;
        this.publicationRegistration?.Dispose();
        this.publicationRegistration = publication.RegisterPreview(project, () => this.CreatePublicationPreviewAsync(project));
        return await this.StartEngineAndContentAsync(cancellationToken).ConfigureAwait(true);
    }

    /// <summary>Retries what failed: the engine and the content when the engine is unavailable, the content otherwise.</summary>
    /// <param name="cancellationToken">Cancels waiting for a start already under way.</param>
    /// <returns><see langword="true"/> when the preview is ready.</returns>
    public Task<bool> RetryAsync(CancellationToken cancellationToken = default)
        => this.project is null || this.disposed
            ? Task.FromResult(false)
            : this.State switch
            {
                PreviewSessionState.EngineUnavailable => this.StartEngineAndContentAsync(cancellationToken),
                PreviewSessionState.ContentUnavailable => this.MountContentAsync(),
                _ => Task.FromResult(this.State == PreviewSessionState.Ready),
            };

    /// <inheritdoc/>
    public async Task<bool> ChangeMountsAsync(ProjectContext expected, ProjectInfo candidate, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(expected);
        ArgumentNullException.ThrowIfNull(candidate);
        var activated = false;
        await coordinator.RunProjectChangeAsync(
            async (operation, token) => activated = await this.owner.InvokeAsync(() => this.ChangeMountsCoreAsync(expected, candidate, operation, token)).ConfigureAwait(true),
            cancellationToken).ConfigureAwait(true);
        return activated;
    }

    /// <inheritdoc/>
    public async ValueTask DisposeAsync()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.publicationRegistration?.Dispose();
        this.publicationRegistration = null;
        if (engine.State == EngineServiceState.Running)
        {
            await this.ReleaseCookedRootsAsync().ConfigureAwait(true);
        }
    }

    private bool IsCurrent(ProjectContext candidate)
        => !this.disposed && this.publicationRegistration is not null && ReferenceEquals(candidate, projects.ActiveProject);

    private Task<ICookPublicationPreview?> CreatePublicationPreviewAsync(ProjectContext candidate)
        => this.owner.InvokeAsync(() => this.IsCurrent(candidate)
            ? (ICookPublicationPreview?)new PublicationPreview(
                candidate,
                engine,
                this.owner,
                catalog,
                () => this.IsCurrent(candidate),
                this.OnContentRecovered)
            : null);

    private void OnContentRecovered()
    {
        if (this.State == PreviewSessionState.ContentUnavailable)
        {
            this.Failure = null;
            this.State = PreviewSessionState.Ready;
        }
    }

    private async Task<bool> StartEngineAndContentAsync(CancellationToken cancellationToken)
    {
        this.State = PreviewSessionState.Starting;
        this.Failure = null;
        return await this.StartEngineAsync(cancellationToken).ConfigureAwait(true)
            && await this.MountContentAsync().ConfigureAwait(true);
    }

    private async Task<bool> ChangeMountsCoreAsync(ProjectContext expected, ProjectInfo candidate, ContentCookOperation operation, CancellationToken token)
    {
        if (!ReferenceEquals(expected, operation.Project) || !ReferenceEquals(expected, projects.ActiveProject))
        {
            throw new OperationCanceledException("The project configuration changed before this mount operation started.");
        }

        var next = MountChange.CandidateContext(expected, candidate);
        using var accepted = await publication.ChangeMountsAsync(operation, next, MountChange.ProjectInfoOf(expected, projects.Settings), candidate, token).ConfigureAwait(true);
        if (!ReferenceEquals(expected, projects.ActiveProject))
        {
            this.LogMountsChangeSuperseded(expected.ProjectRoot.FullPath);
            return false;
        }

        // Publication has committed both saved intent and native admission; the session follows the new context.
        this.publicationRegistration?.Dispose();
        this.publicationRegistration = publication.RegisterPreview(next, () => this.CreatePublicationPreviewAsync(next));
        if (projectManager.CurrentProject?.ProjectInfo is { } loaded)
        {
            MountChange.UpdateLoadedMounts(loaded, candidate);
        }

        this.project = next;
        projects.Activate(next);
        this.LogMountsChanged(next.ProjectRoot.FullPath, next.LocalFolderMounts.Count, next.CookedContentOrder.Count);
        await catalog.UsePublicationAsync(accepted, CancellationToken.None).ConfigureAwait(true);
        return true;
    }
}
