// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookCoordinatorScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookPriorityTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Explicit and demanded work passes queued saves, preserving order within each priority.</summary>
    /// <returns>The asynchronous queue-order regression.</returns>
    [TestMethod]
    public async Task PreviewAndExplicitCooksRunBeforeWaitingSaves()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var active = coordinator.RunAsync((_, _) => release.Task, this.TestContext.CancellationToken);
        var order = new List<string>();
        var firstSave = Queue("save-1", automatic: true);
        var demand = Queue("demand", automatic: true, demand: true);
        var secondSave = Queue("save-2", automatic: true);
        var explicitCook = Queue("explicit", automatic: false);
        _ = order.Should().BeEmpty();
        release.SetResult(0);
        _ = await Task.WhenAll(active, firstSave, demand, secondSave, explicitCook).ConfigureAwait(false);
        _ = order.Should().Equal("demand", "explicit", "save-1", "save-2");

        Task<int> Queue(string name, bool automatic, bool demand = false)
            => coordinator.RunCookAsync(
                new(CookTargetKind.Asset, AssetPath.Parse("/Content/" + name + ".omat.json"), automatic) { IsDemand = demand },
                (_, _) =>
                {
                    order.Add(name);
                    return Task.FromResult(1);
                },
                this.TestContext.CancellationToken);
    }

    /// <summary>A cook the user asks for stops a running cook on change, which ends as stopped and tells its caller.</summary>
    /// <returns>The asynchronous foreground regression.</returns>
    [TestMethod]
    public async Task RequestedCookStopsARunningCookOnChange()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var automatic = coordinator.RunCookAsync(
            new(CookTargetKind.Asset, AssetPath.Parse("/Content/Saved.omat.json"), IsAutomatic: true) { CoalescePending = true },
            async (_, token) =>
            {
                started.SetResult();
                await Task.Delay(Timeout.Infinite, token).ConfigureAwait(false);
                return 1;
            },
            this.TestContext.CancellationToken);
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        var requested = coordinator.RunCookAsync(
            new(CookTargetKind.Asset, AssetPath.Parse("/Content/Other.omat.json")) { CoalescePending = true },
            (_, _) => Task.FromResult(2),
            this.TestContext.CancellationToken);

        Func<Task> stopped = async () => await automatic.ConfigureAwait(false);
        _ = await stopped.Should().ThrowAsync<CookPreemptedException>().ConfigureAwait(false);
        _ = (await requested.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(2);
        _ = coordinator.Runs.Single(run => run.Request.IsAutomatic).State.Should().Be(CookRunState.Cancelled);
    }

    /// <summary>Only a requested cook stops a cook on change; neither another cook on change nor a preview one is stopped.</summary>
    /// <returns>The asynchronous non-preemption regression.</returns>
    [TestMethod]
    public async Task PreviewAndCookOnChangeAreNotStoppedByEachOther()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var started = new TaskCompletionSource<CancellationToken>(TaskCreationOptions.RunContinuationsAsynchronously);
        var preview = coordinator.RunCookAsync(
            new(CookTargetKind.Asset, AssetPath.Parse("/Content/Preview.omat.json"), IsAutomatic: true) { IsDemand = true },
            async (_, token) =>
            {
                started.SetResult(token);
                await release.Task.ConfigureAwait(false);
                return 1;
            },
            this.TestContext.CancellationToken);
        var token = await started.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        var saved = coordinator.RunCookAsync(new(CookTargetKind.Asset, AssetPath.Parse("/Content/Saved.omat.json"), IsAutomatic: true), (_, _) => Task.FromResult(2), this.TestContext.CancellationToken);
        var requested = coordinator.RunCookAsync(new(CookTargetKind.Asset, AssetPath.Parse("/Content/Other.omat.json")), (_, _) => Task.FromResult(3), this.TestContext.CancellationToken);

        _ = token.IsCancellationRequested.Should().BeFalse();
        release.SetResult();
        _ = (await Task.WhenAll(preview, saved, requested).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(1, 2, 3);
    }

    /// <summary>The writer is idle only once no operation holds it or waits for it.</summary>
    /// <returns>The asynchronous idle regression.</returns>
    [TestMethod]
    public async Task WriterIsIdleOnlyWhenNothingHoldsOrWaitsForIt()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        await coordinator.WhenWriterIdleAsync(this.TestContext.CancellationToken).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var hold = WriterHold.Take(coordinator, this.TestContext.CancellationToken);
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var queued = coordinator.RunAsync((_, _) => release.Task, this.TestContext.CancellationToken);

        var idle = coordinator.WhenWriterIdleAsync(this.TestContext.CancellationToken);
        await hold.ReleaseAsync().ConfigureAwait(false);
        _ = idle.IsCompleted.Should().BeFalse("the queued operation takes the writer next");

        release.SetResult(1);
        _ = await queued.ConfigureAwait(false);
        await idle.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    /// <summary>Removing a cancelled priority waiter allows the next save to acquire the writer.</summary>
    /// <returns>The asynchronous cancellation regression.</returns>
    [TestMethod]
    public async Task CancelledDemandDoesNotConsumeWriterOrBlockQueuedSave()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        using var cancelled = new CancellationTokenSource();
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var active = coordinator.RunAsync((_, _) => release.Task, this.TestContext.CancellationToken);
        var save = coordinator.RunCookAsync(
            new(CookTargetKind.Asset, AssetPath.Parse("/Content/Saved.omat.json"), IsAutomatic: true),
            (_, _) => Task.FromResult(1),
            this.TestContext.CancellationToken);
        var entered = false;
        var demand = coordinator.RunCookAsync(
            new(CookTargetKind.Asset, AssetPath.Parse("/Content/Preview.omat.json"), IsAutomatic: true) { IsDemand = true },
            (_, _) =>
            {
                entered = true;
                return Task.FromResult(2);
            },
            cancelled.Token);
        await cancelled.CancelAsync().ConfigureAwait(false);
        Func<Task> completion = async () => await demand.ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        release.SetResult(0);
        _ = await active.ConfigureAwait(false);
        _ = (await save.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(1);
        _ = entered.Should().BeFalse();
    }

    /// <summary>A mount change cancels the queued requests, which cannot strand the requests made after it.</summary>
    /// <returns>The asynchronous project-lifetime regression.</returns>
    [TestMethod]
    public async Task MountChangeCancelsQueuedRequests()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        var hold = WriterHold.Take(coordinator, this.TestContext.CancellationToken);
        var request = new CookRunRequest(CookTargetKind.Asset, AssetPath.Parse("/Content/Preview.omat.json"), IsAutomatic: true) { IsDemand = true };
        var old = coordinator.RunCookAsync(request, (_, _) => Task.FromResult(1), this.TestContext.CancellationToken);
        context.ChangeContext(context.Context with { });
        Func<Task> completion = async () => await old.ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        Func<Task> held = async () => await hold.Held.ConfigureAwait(false);
        _ = await held.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = (await coordinator.RunCookAsync(request, (_, _) => Task.FromResult(2), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(2);
    }
}
