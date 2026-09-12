// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Coalesces Save requests per source and uses the shared cook writer and run history.</summary>
public sealed partial class AutomaticCookService : IAutomaticCookService, IObserver<ProjectContext?>, IDisposable
{
    private readonly IProjectContextService projects;
    private readonly IContentPipelineService pipeline;
    private readonly ICookRunService runs;
    private readonly ILogger<AutomaticCookService> logger;
    private readonly Lock sync = new();
    private readonly Dictionary<Uri, SavedRequest> requests = [];
    private readonly IDisposable subscription;
    private ProjectContext? project;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="AutomaticCookService"/> class.</summary>
    /// <param name="projects">The active project and its lifetime notifications.</param>
    /// <param name="pipeline">The same saved-input pipeline used by explicit cooks.</param>
    /// <param name="runs">The shared run history and save recovery controls.</param>
    /// <param name="logger">Reports scheduling failures without failing an acknowledged Save.</param>
    public AutomaticCookService(IProjectContextService projects, IContentPipelineService pipeline, ICookRunService runs, ILogger<AutomaticCookService> logger)
    {
        this.projects = projects;
        this.pipeline = pipeline;
        this.runs = runs;
        this.logger = logger;
        this.subscription = projects.ProjectChanged.Subscribe(this);
    }

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Scheduling derived work must never change an acknowledged source Save into a failure.")]
    public void NotifySaved(string sourcePath, string contentHash, bool contentChanged = true)
    {
        try
        {
            this.QueueSaved(sourcePath, contentHash, contentChanged);
        }
        catch (Exception exception)
        {
            this.LogSchedulingFailure(exception, sourcePath);
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        lock (this.sync)
        {
            this.disposed = true;
            this.requests.Clear();
        }

        this.subscription.Dispose();
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    void IObserver<ProjectContext?>.OnNext(ProjectContext? value)
    {
        lock (this.sync)
        {
            this.project = value;
            this.requests.Clear();
        }
    }

    /// <inheritdoc />
    void IObserver<ProjectContext?>.OnCompleted() => ((IObserver<ProjectContext?>)this).OnNext(value: null);

    /// <inheritdoc />
    void IObserver<ProjectContext?>.OnError(Exception error) => ((IObserver<ProjectContext?>)this).OnNext(value: null);

    private static Uri? ResolveSource(ProjectContext project, string sourcePath)
    {
        foreach (var mount in project.AuthoringMounts.OrderByDescending(static item => item.RelativePath.Length))
        {
            var root = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath));
            var relative = Path.GetRelativePath(root, sourcePath);
            if (!Path.IsPathRooted(relative) && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal) && !string.Equals(relative, "..", StringComparison.Ordinal))
            {
                return new Uri($"{AssetUris.Scheme}:///{Uri.EscapeDataString(mount.Name)}/{string.Join('/', relative.Split(Path.DirectorySeparatorChar).Select(Uri.EscapeDataString))}");
            }
        }

        return null;
    }

    private void QueueSaved(string sourcePath, string contentHash, bool contentChanged)
    {
        SavedRequest request;
        lock (this.sync)
        {
            if (this.disposed || this.project is not { } current || !ReferenceEquals(current, this.projects.ActiveProject)
                || ResolveSource(current, sourcePath) is not { } uri)
            {
                return;
            }

            // A pending explicit or automatic cook already owns this saved dependency.
            var resumed = false;
            foreach (var run in this.runs.Runs.Where(run => run.ProjectId == current.ProjectId && run.State == CookRunState.NeedsSave
                && run.UnsavedDocuments.Any(document => string.Equals(document.SourcePath, sourcePath, StringComparison.OrdinalIgnoreCase))))
            {
                resumed |= this.runs.ResumeAfterSave(run.OperationId);
            }

            if (!contentChanged)
            {
                return;
            }

            if (this.requests.TryGetValue(uri, out var existing))
            {
                if (string.Equals(existing.ContentHash, contentHash, StringComparison.Ordinal))
                {
                    return;
                }

                existing.ContentHash = contentHash;
                if (existing.Running)
                {
                    return;
                }
            }

            request = new(current, uri, contentHash) { Running = !resumed };
            this.requests[uri] = request;
            if (resumed)
            {
                return;
            }
        }

        // Leave document save gates before source capture starts; this task owns and observes its failures.
        _ = Task.Run(() => this.CookSavedAsync(request), CancellationToken.None);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The shared coordinator records background failures in Cooking; they must not escape onto the document's Save caller.")]
    private async Task CookSavedAsync(SavedRequest request)
    {
        while (true)
        {
            string capturedHash;
            lock (this.sync)
            {
                if (this.disposed || !ReferenceEquals(request.Project, this.project))
                {
                    return;
                }

                capturedHash = request.ContentHash;
            }

            try
            {
                var result = await this.pipeline.CookSavedAssetAsync(request.AssetUri, request.Project, CancellationToken.None).ConfigureAwait(false);
                capturedHash = result.InputSnapshot?.Inputs.FirstOrDefault(input => input.AssetUri == request.AssetUri)?.DiscoveryHash ?? capturedHash;
            }
            catch (OperationCanceledException)
            {
                // Cancellation remains visible in the run history; only a later changed Save can request new work.
            }
            catch (Exception exception)
            {
                this.LogSchedulingFailure(exception, request.AssetUri.AbsoluteUri);
            }

            lock (this.sync)
            {
                if (this.disposed || !ReferenceEquals(request.Project, this.project))
                {
                    return;
                }

                if (string.Equals(request.ContentHash, capturedHash, StringComparison.Ordinal))
                {
                    request.Running = false;
                    return;
                }
            }
        }
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "Automatic cooking failed for saved source {Source}.")]
    private partial void LogSchedulingFailure(Exception exception, string source);

    private sealed class SavedRequest(ProjectContext project, Uri assetUri, string contentHash)
    {
        public ProjectContext Project { get; } = project;

        public Uri AssetUri { get; } = assetUri;

        public string ContentHash { get; set; } = contentHash;

        public bool Running { get; set; }
    }
}
