// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>Follows the engine run the session's content is mounted on, and makes the engine unavailable when it ends.</summary>
public sealed partial class PreviewSession
{
    // The engine run the session started or found running, until it ends.
    private Guid? run;

    // The end of that run while the session's own start is still mounting; the start reports it when it finishes.
    private EngineStateChangedEventArgs? endedRun;

    private void OnEngineStateChanged(object? sender, EngineStateChangedEventArgs args)
        => this.owner.Post(() => this.ObserveEngineState(args));

    private void ObserveEngineState(EngineStateChangedEventArgs args)
    {
        if (this.disposed || args.PreviousState != EngineServiceState.Running || args.State == EngineServiceState.Running
            || args.RunId != this.run)
        {
            return;
        }

        this.run = null;
        if (this.State == PreviewSessionState.Starting)
        {
            this.endedRun = args;
        }
        else if (this.State is PreviewSessionState.Ready or PreviewSessionState.ContentUnavailable)
        {
            this.ReportEndedRun(args);
        }
    }

    // Called by the start once the engine runs; it is on the owner thread, so the end of this run is handled after it.
    private void FollowRun()
    {
        this.run = engine.WorldCommands.RunId;
        this.endedRun = null;
    }

    // Called by the start when its mount finishes: a run that ended meanwhile makes the engine unavailable.
    private bool ReportEndedRunAfterStart()
    {
        if (this.endedRun is not { } ended)
        {
            return false;
        }

        this.endedRun = null;
        this.ReportEndedRun(ended);
        return true;
    }

    // The engine service publishes a run that ends unexpectedly; a run shut down on request is the session's to report.
    private void ReportEndedRun(EngineStateChangedEventArgs ended)
    {
        if (ended.OperationResult is { } result)
        {
            var code = result.Diagnostics[0].Code;
            this.LogEngineRunFaulted(ended.RunId, ended.State, code);
            this.Failure = new PreviewFailure(code, result.Title, result.Message);
        }
        else
        {
            const string title = "Embedded runtime stopped";
            const string message = "The live engine runtime was shut down while the project was open. Authoring still works.";
            this.LogEngineRunStopped(ended.RunId, ended.State);
            this.Failure = new PreviewFailure(RuntimeDiagnosticCodes.Stopped, title, message);
            RuntimeOperationResults.PublishWarning(
                results,
                status,
                RuntimeOperationKinds.Loop,
                FailureDomain.RuntimeExecution,
                RuntimeDiagnosticCodes.Stopped,
                title,
                message,
                this.ProjectScope());
        }

        this.State = PreviewSessionState.EngineUnavailable;
    }
}
