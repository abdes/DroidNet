// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.LivePreview.Tests.TestSupport;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Tests.Session;

/// <summary>
/// Checks an engine run that ends after the session started: an engine that leaves <c>Running</c> outside the session's
/// own start makes the engine unavailable, with a failure code and a published result, and Retry restarts the engine
/// and remounts the content (LLD live-preview 5.6).
/// </summary>
internal sealed partial class PreviewSessionTests
{
    [TestMethod]
    public async Task EngineFaultAfterReady_LeavesTheEngineUnavailableAndRetryRestartsAndRemounts()
    {
        using var fixture = new SessionFixture(EngineServiceState.Running);
        _ = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        fixture.Engine.ChangeState(EngineServiceState.Faulted, LoopFailure());

        _ = fixture.Session.State.Should().Be(PreviewSessionState.EngineUnavailable);
        _ = fixture.Session.Failure!.Code.Should().Be(RuntimeDiagnosticCodes.LoopFaulted, "the engine service owns and publishes the run's failure");
        _ = Codes(fixture).Should().NotContain(RuntimeDiagnosticCodes.LoopFaulted, "the session does not publish the engine's result a second time");

        _ = (await fixture.Session.RetryAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Session.State.Should().Be(PreviewSessionState.Ready, fixture.Session.Failure?.Message);
        _ = fixture.Session.Failure.Should().BeNull();
        _ = fixture.Engine.Starts.Should().Be(1);
        _ = fixture.Engine.MountedRoots.Should().HaveCount(2, "the new run mounts the content again");
    }

    [TestMethod]
    public async Task EngineStoppedAfterReady_LeavesTheEngineUnavailableWithAPublishedWarning()
    {
        using var fixture = new SessionFixture(EngineServiceState.Running);
        _ = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        fixture.Engine.ChangeState(EngineServiceState.ShuttingDown);
        fixture.Engine.ChangeState(EngineServiceState.NoEngine);

        _ = fixture.Session.State.Should().Be(PreviewSessionState.EngineUnavailable);
        _ = fixture.Session.Failure!.Code.Should().Be(RuntimeDiagnosticCodes.Stopped);
        _ = fixture.Results.Should().ContainSingle(static result => result.Diagnostics.Any(static diagnostic => diagnostic.Code == RuntimeDiagnosticCodes.Stopped))
            .Which.Severity.Should().Be(DiagnosticSeverity.Warning);

        _ = (await fixture.Session.RetryAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Session.State.Should().Be(PreviewSessionState.Ready, fixture.Session.Failure?.Message);
        _ = fixture.Engine.Starts.Should().Be(1);
    }

    [TestMethod]
    public async Task EngineFaultWhileContentIsUnavailable_LeavesTheEngineUnavailable()
    {
        using var fixture = new SessionFixture(EngineServiceState.Running);
        fixture.FailCatalog = true;
        _ = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.FailCatalog = false;

        fixture.Engine.ChangeState(EngineServiceState.Faulted, LoopFailure());

        _ = fixture.Session.State.Should().Be(PreviewSessionState.EngineUnavailable, "remounting alone cannot recover a run that ended");
        _ = (await fixture.Session.RetryAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Engine.Starts.Should().Be(1);
    }

    [TestMethod]
    public async Task EngineFaultWhileMounting_LeavesTheEngineUnavailableOnceTheMountEnds()
    {
        using var fixture = new SessionFixture(EngineServiceState.Running);
        fixture.WhileMounting = () =>
        {
            fixture.WhileMounting = null;
            fixture.Engine.ChangeState(EngineServiceState.Faulted, LoopFailure());
        };

        var ready = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = ready.Should().BeFalse();
        _ = fixture.Session.State.Should().Be(PreviewSessionState.EngineUnavailable);
        _ = fixture.Session.Failure!.Code.Should().Be(RuntimeDiagnosticCodes.LoopFaulted);
    }

    [TestMethod]
    public async Task EndOfAnotherRun_ChangesNothing()
    {
        using var fixture = new SessionFixture(EngineServiceState.Running);
        _ = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var results = fixture.Results.Count;

        fixture.Engine.World.RunId = Guid.NewGuid();
        fixture.Engine.ChangeState(EngineServiceState.Faulted, LoopFailure());

        _ = fixture.Session.State.Should().Be(PreviewSessionState.Ready);
        _ = fixture.Results.Should().HaveCount(results);
    }

    [TestMethod]
    public async Task EngineFaultDuringTheSessionsOwnStart_IsReportedAsAStartFailure()
    {
        using var fixture = new SessionFixture(EngineServiceState.NoEngine);
        fixture.Engine.StartFailure = new InvalidOperationException("No device.");

        _ = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = fixture.Session.Failure!.Code.Should().Be(RuntimeDiagnosticCodes.StartFailed);
        _ = Codes(fixture).Should().ContainSingle();
    }

    [TestMethod]
    public async Task EngineFaultAfterDisposal_ChangesNothing()
    {
        var fixture = new SessionFixture(EngineServiceState.Running);
        _ = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await fixture.Session.DisposeAsync().ConfigureAwait(false);
        var results = fixture.Results.Count;

        fixture.Engine.ChangeState(EngineServiceState.Faulted, LoopFailure());

        _ = fixture.Session.State.Should().Be(PreviewSessionState.Ready);
        _ = fixture.Results.Should().HaveCount(results);
        fixture.Dispose();
    }

    [TestMethod]
    public async Task EngineFaultRaisedOnAnotherThread_ChangesTheStateOnTheOwnerThread()
    {
        using var owner = new OwnerContext();
        var fixture = await owner.RunAsync(static () => Task.FromResult(new SessionFixture(EngineServiceState.Running))).ConfigureAwait(false);
        _ = await owner.RunAsync(() => fixture.Session.StartAsync(this.TestContext.CancellationToken)).ConfigureAwait(false);
        var threads = new List<int>();
        fixture.Session.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName == nameof(PreviewSession.State))
            {
                threads.Add(Environment.CurrentManagedThreadId);
            }
        };

        await Task.Run(() => fixture.Engine.ChangeState(EngineServiceState.Faulted, LoopFailure()), this.TestContext.CancellationToken).ConfigureAwait(false);
        await owner.DrainAsync().ConfigureAwait(false);

        _ = fixture.Session.State.Should().Be(PreviewSessionState.EngineUnavailable);
        _ = threads.Should().Equal(owner.ThreadId);
        await owner.RunAsync(() =>
        {
            fixture.Dispose();
            return Task.CompletedTask;
        }).ConfigureAwait(false);
    }

    private static IEnumerable<string> Codes(SessionFixture fixture)
        => fixture.Results.SelectMany(static result => result.Diagnostics).Select(static diagnostic => diagnostic.Code);

    // The result the engine service publishes when a run's loop fails.
    private static OperationResult LoopFailure()
    {
        var operation = Guid.NewGuid();
        return new OperationResult
        {
            OperationId = operation,
            OperationKind = RuntimeOperationKinds.Loop,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = "Engine preview stopped unexpectedly",
            Message = "Device removed.",
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operation,
                    Domain = FailureDomain.RuntimeExecution,
                    Severity = DiagnosticSeverity.Error,
                    Code = RuntimeDiagnosticCodes.LoopFaulted,
                    Message = "Device removed.",
                },
            ],
        };
    }
}
