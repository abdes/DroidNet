// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>Starts the engine and mounts the project's cooked content, reporting what fails.</summary>
public sealed partial class PreviewSession
{
    private const string EngineStartFailedCode = DiagnosticCodes.RuntimePrefix + "START_FAILED";

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "An engine that cannot start leaves authoring working; the failure is published and can be retried.")]
    private async Task<bool> StartEngineAsync(CancellationToken cancellationToken)
    {
        await this.startGate.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            switch (engine.State)
            {
                case EngineServiceState.Running:
                    return true;

                case EngineServiceState.NoEngine:
                case EngineServiceState.Faulted:
                    this.LogEngineStarting();
                    _ = await engine.InitializeAsync(cancellationToken).ConfigureAwait(true);
                    await engine.StartAsync().ConfigureAwait(true);
                    break;

                case EngineServiceState.Ready:
                    this.LogInitializedEngineStarting();
                    await engine.StartAsync().ConfigureAwait(true);
                    break;

                default:
                    this.LogEngineStateNotReady(engine.State);
                    break;
            }

            if (engine.State == EngineServiceState.Running)
            {
                return true;
            }

            this.FailEngine(exception: null);
            return false;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception exception)
        {
            this.LogEngineStartFailed(exception);
            this.FailEngine(exception);
            return false;
        }
        finally
        {
            _ = this.startGate.Release();
        }
    }

    private void FailEngine(Exception? exception)
    {
        const string title = "Embedded runtime failed to start";
        const string message = "The workspace opened, but the live engine runtime could not start.";
        this.Failure = new PreviewFailure(EngineStartFailedCode, title, message);
        this.State = PreviewSessionState.EngineUnavailable;
        RuntimeOperationResults.PublishFailure(
            results,
            status,
            RuntimeOperationKinds.Start,
            FailureDomain.RuntimeDiscovery,
            EngineStartFailedCode,
            title,
            message,
            this.ProjectScope(),
            exception: exception);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Content that cannot be mounted leaves authoring working; the failure is published and can be retried.")]
    private async Task<bool> MountContentAsync()
    {
        if (projects.ActiveProject is not { } active)
        {
            this.LogNoActiveProject();
            return false;
        }

        var stage = "reading the cooked content";
        var mounted = false;
        try
        {
            var (reader, set) = await this.AdmitCookedContentAsync(active).ConfigureAwait(true);
            using var retained = reader;
            var bindings = RuntimeBindings.Of(reader);
            stage = "mounting the content in the engine";
            await engine.RefreshProjectCookedRootsAsync(bindings, set).ConfigureAwait(true);
            mounted = true;
            stage = "refreshing the asset catalog";
            await catalog.UsePublicationAsync(reader, CancellationToken.None).ConfigureAwait(true);
            this.Failure = null;
            this.State = PreviewSessionState.Ready;
            this.LogMountedRoots(set.Roots.Count);
            await this.MaintainPublicationAsync(active).ConfigureAwait(true);
            return true;
        }
        catch (Exception exception)
        {
            this.LogCookedRootRefreshFailed(exception, stage, active.ProjectRoot.FullPath, mounted ? "mounted" : "unavailable");
            var title = mounted ? "Content Explorer unavailable" : "Preview unavailable";
            var message = $"Failed while {stage}: {exception.Message.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries).FirstOrDefault()}";
            RuntimeOperationResults.PublishFailure(
                results,
                status,
                RuntimeOperationKinds.CookedRootRefresh,
                FailureDomain.AssetMount,
                AssetMountDiagnosticCodes.RefreshFailed,
                title,
                message,
                this.ProjectScope(),
                active.ProjectRoot.FullPath,
                exception,
                exception.ToString());
            if (!mounted && !await this.SuspendCookedContentAsync(active).ConfigureAwait(true))
            {
                message += " Preview suspension also failed; see Details.";
            }

            this.Failure = new PreviewFailure(AssetMountDiagnosticCodes.RefreshFailed, title, message);
            this.State = PreviewSessionState.ContentUnavailable;
            return false;
        }
    }

    /// <summary>Publishes from the current libraries and admits that selection, republishing once when a library is re-cooked in between.</summary>
    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "The admitted reader transfers to the caller; every rejected reader is disposed here.")]
    private async Task<(CookPublicationReadLease Reader, CookedContentMountSet Mounts)> AdmitCookedContentAsync(ProjectContext active)
    {
        var reader = await publication.AcquireForMountAsync(active, CancellationToken.None).ConfigureAwait(true);
        try
        {
            try
            {
                return (reader, await mounts.PrepareAsync(active, reader, CancellationToken.None).ConfigureAwait(true));
            }
            catch (CookedLibraryChangedException changed)
            {
                this.LogCookedLibraryChanged(changed, active.ProjectRoot.FullPath);
                reader.Dispose();
                reader = await publication.AcquireForMountAsync(active, CancellationToken.None).ConfigureAwait(true);
                return (reader, await mounts.PrepareAsync(active, reader, CancellationToken.None).ConfigureAwait(true));
            }
        }
        catch
        {
            reader.Dispose();
            throw;
        }
    }

    private async Task MaintainPublicationAsync(ProjectContext active)
    {
        try
        {
            var cleanupFailures = await publication.MaintainAsync(active, CancellationToken.None).ConfigureAwait(true);
            if (cleanupFailures.Count != 0)
            {
                this.PublishCleanupWarning(string.Join(Environment.NewLine, cleanupFailures), active.ProjectRoot.FullPath);
            }
        }
        catch (Exception cleanup) when (cleanup is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException)
        {
            this.PublishCleanupWarning(cleanup.Message, active.ProjectRoot.FullPath, cleanup);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A secondary suspension failure is published without hiding the mount failure that caused it.")]
    private async Task<bool> SuspendCookedContentAsync(ProjectContext active)
    {
        try
        {
            await engine.SuspendCookedContentAsync().ConfigureAwait(true);
            return true;
        }
        catch (Exception failure)
        {
            this.LogCookedContentSuspensionFailed(failure, active.ProjectRoot.FullPath);
            RuntimeOperationResults.PublishFailure(
                results,
                status,
                RuntimeOperationKinds.CookedRootRefresh,
                FailureDomain.AssetMount,
                AssetMountDiagnosticCodes.RefreshFailed,
                "Preview suspension failed",
                "The preview could not be suspended after content refresh failed.",
                this.ProjectScope(),
                active.ProjectRoot.FullPath,
                failure,
                failure.ToString());
            return false;
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Closing the project observes native teardown failures; the runtime keeps its readers until it can clean up safely.")]
    private async Task ReleaseCookedRootsAsync()
    {
        try
        {
            await engine.RefreshProjectCookedRootsAsync([]).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            this.LogCookedRootReleaseFailed(exception);
        }
    }

    private AffectedScope ProjectScope()
        => projects.ActiveProject is { } active
            ? new AffectedScope
            {
                ProjectId = active.ProjectId,
                ProjectName = active.Name,
                ProjectPath = active.ProjectRoot.FullPath,
            }
            : AffectedScope.Empty;

    private void PublishCleanupWarning(string message, string affectedPath, Exception? exception = null)
        => RuntimeOperationResults.PublishWarning(
            results,
            status,
            RuntimeOperationKinds.CookedRootRefresh,
            FailureDomain.AssetMount,
            "Cook.CleanupDeferred",
            "Unused output cleanup deferred",
            message,
            this.ProjectScope(),
            affectedPath,
            exception);
}
