// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Tests.TestSupport;

namespace Oxygen.Editor.Scenes.Tests.Authoring;

/// <summary>
/// Rule 4 (scene-edits.md 3.1): a caller on any thread may author; the scene is only touched, and its changes only
/// raised, on the thread it was decoded on.
/// </summary>
/// <remarks>
/// Each test opens its scene with <see cref="AuthoringFixture.CreateOnThreadAsync"/> and calls the frame from a pool
/// thread with <see cref="Task.Run(Func{Task})"/>, as a remote client would.
/// </remarks>
[TestClass]
public sealed class SceneThreadTests
{
    /// <summary>A frame call made from the thread pool.</summary>
    public enum Operation
    {
        /// <summary><see cref="SceneAuthoring.UndoAsync"/> of the probe.</summary>
        Undo,

        /// <summary><see cref="SceneAuthoring.RedoAsync"/> of the probe.</summary>
        Redo,

        /// <summary><see cref="SceneAuthoring.ValidateAsync"/> of the probe.</summary>
        Validate,
    }

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task Execute_FromThreadPool_RunsAndRaisesOnTheSceneThread()
    {
        using var fixture = await AuthoringFixture.CreateOnThreadAsync(SampleScene.Build()).ConfigureAwait(false);
        var probe = new ProbeEdit();

        var result = await Task.Run(() => fixture.Authoring.ExecuteAsync(probe), this.TestContext.CancellationToken).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = probe.Log.Calls.Select(static call => call.ThreadId).Should().Equal(fixture.Thread!.ThreadId, fixture.Thread.ThreadId);
        _ = fixture.Changes.All.Should().ContainSingle().Which.ThreadId.Should().Be(fixture.Thread.ThreadId);
    }

    [TestMethod]
    [DataRow(Operation.Undo)]
    [DataRow(Operation.Redo)]
    [DataRow(Operation.Validate)]
    public async Task Operation_FromThreadPool_RunsOnTheSceneThread(Operation operation)
    {
        using var fixture = await AuthoringFixture.CreateOnThreadAsync(SampleScene.Build()).ConfigureAwait(false);
        var probe = new ProbeEdit();
        _ = await fixture.Authoring.ExecuteAsync(probe).ConfigureAwait(false);
        if (operation == Operation.Redo)
        {
            _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        }

        var calls = probe.Log.Calls.Count;
        Func<Task> operate = operation switch
        {
            Operation.Undo => fixture.Authoring.UndoAsync,
            Operation.Redo => fixture.Authoring.RedoAsync,
            _ => () => fixture.Authoring.ValidateAsync(probe),
        };

        await Task.Run(operate, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = probe.Log.Calls.Skip(calls).Should().NotBeEmpty().And.AllSatisfy(call => call.ThreadId.Should().Be(fixture.Thread!.ThreadId));
    }

    [TestMethod]
    public async Task FiftyExecutes_FromParallelThreads_NeverOverlap()
    {
        using var fixture = await AuthoringFixture.CreateOnThreadAsync(SampleScene.Build()).ConfigureAwait(false);
        var active = 0;
        var overlapped = false;
        var log = new ProbeLog();
        var probe = new ProbeEdit
        {
            Log = log,
            Effect = scope =>
            {
                overlapped |= Interlocked.Increment(ref active) > 1;
                Thread.SpinWait(1000);
                _ = Interlocked.Decrement(ref active);
            },
        };

        var results = await Task.WhenAll(Enumerable.Range(0, 50).Select(_ => Task.Run(() => fixture.Authoring.ExecuteAsync(probe), this.TestContext.CancellationToken))).ConfigureAwait(false);

        _ = results.Should().AllSatisfy(static result => result.Outcome.Should().Be(SceneEditOutcome.Applied));
        _ = overlapped.Should().BeFalse();
        _ = log.Calls.Count(static call => call.Step == ProbeStep.Apply).Should().Be(50);
        _ = fixture.History.UndoStack.Should().HaveCount(50);
    }

    [TestMethod]
    public async Task Execute_OnTheSceneThread_CompletesSynchronously()
    {
        using var fixture = await AuthoringFixture.CreateOnThreadAsync(SampleScene.Build()).ConfigureAwait(false);

        var completedOnReturn = await fixture.Thread!.RunAsync(() => Task.FromResult(fixture.Authoring.ExecuteAsync(new ProbeEdit()).IsCompleted)).ConfigureAwait(false);

        _ = completedOnReturn.Should().BeTrue();
    }

    [TestMethod]
    public async Task Execute_ApplyThrowsOnTheSceneThread_FaultsTheCallersTask()
    {
        using var fixture = await AuthoringFixture.CreateOnThreadAsync(SampleScene.Build()).ConfigureAwait(false);
        var failure = new InvalidOperationException("probe failure");

        var execute = () => Task.Run(() => fixture.Authoring.ExecuteAsync(new ProbeEdit { Failure = failure }), this.TestContext.CancellationToken);

        _ = (await execute.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false)).Which.Should().BeSameAs(failure);
        _ = fixture.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task NoContextAtDecode_RunsOnTheCallersThread()
    {
        using var fixture = await Task.Run(() => AuthoringFixture.CreateAsync(SampleScene.Build()), this.TestContext.CancellationToken).ConfigureAwait(false);
        var probe = new ProbeEdit();
        var callerThread = await Task.Run(ExecuteOnCallerAsync, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = probe.Log.Calls.Should().AllSatisfy(call => call.ThreadId.Should().Be(callerThread));

        async Task<int> ExecuteOnCallerAsync()
        {
            _ = await fixture.Authoring.ExecuteAsync(probe).ConfigureAwait(false);
            return Environment.CurrentManagedThreadId;
        }
    }
}
