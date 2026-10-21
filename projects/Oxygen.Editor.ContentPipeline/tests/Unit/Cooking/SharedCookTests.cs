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
public sealed class SharedCookTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Two pending callers get one run; cancelling one leaves the other caller's work intact.</summary>
    /// <returns>The asynchronous shared ownership regression.</returns>
    [TestMethod]
    public async Task PendingCookIsSharedAndOneCancelledObserverDoesNotStopIt()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        using var cancelled = new CancellationTokenSource();
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var active = coordinator.RunAsync((_, _) => release.Task, this.TestContext.CancellationToken);
        var calls = 0;
        var first = coordinator.RunCookAsync(SharedAssetRequest(), Work, cancelled.Token);
        var second = coordinator.RunCookAsync(SharedAssetRequest(), Work, this.TestContext.CancellationToken);
        _ = coordinator.Runs.Should().ContainSingle();
        await cancelled.CancelAsync().ConfigureAwait(false);
        Func<Task> firstCompletion = async () => await first.ConfigureAwait(false);
        _ = await firstCompletion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = second.IsCompleted.Should().BeFalse();
        release.SetResult(0);
        _ = await active.ConfigureAwait(false);
        _ = (await second.ConfigureAwait(false)).Should().Be(1);
        _ = calls.Should().Be(1);

        Task<int> Work(ContentCookOperation operation, CancellationToken token)
        {
            coordinator.VerifyWriter(operation);
            token.ThrowIfCancellationRequested();
            calls++;
            return Task.FromResult(calls);
        }
    }

    /// <summary>An explicit caller promotes an equivalent queued automatic cook, which then does the explicit work.</summary>
    /// <returns>The asynchronous promotion regression.</returns>
    [TestMethod]
    public async Task ExplicitRequestPromotesSharedQueuedSaveAndRevealsItsRun()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        var hold = WriterHold.Take(coordinator, this.TestContext.CancellationToken);
        var revealed = false;
        coordinator.RunChanged += (_, args) => revealed |= args.Reveal;
        var save = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(1), this.TestContext.CancellationToken);
        var explicitCook = coordinator.RunCookAsync(SharedAssetRequest() with { IsAutomatic = false }, (_, _) => Task.FromResult(2), this.TestContext.CancellationToken);
        await hold.ReleaseAsync().ConfigureAwait(false);
        _ = (await save.ConfigureAwait(false)).Should().Be(2, "an automatic cook does only part of an explicit cook's work");
        _ = (await explicitCook.ConfigureAwait(false)).Should().Be(2);
        _ = coordinator.Runs.Should().ContainSingle().Which.Request.IsAutomatic.Should().BeFalse();
        _ = revealed.Should().BeTrue();
    }

    /// <summary>A request joins a pending folder cook that covers it; one outside the folder, or a reimport, does not.</summary>
    /// <returns>The asynchronous covering-join regression.</returns>
    [TestMethod]
    public async Task RequestJoinsAPendingCookThatCoversIt()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        var hold = WriterHold.Take(coordinator, this.TestContext.CancellationToken);
        var folder = Queue(new(CookTargetKind.Folder, AssetPath.Parse("/Content/Materials")), 1);
        var asset = Queue(new(CookTargetKind.Asset, AssetPath.Parse("/Content/Materials/Wood.omat.json")), 2);
        var automatic = Queue(new(CookTargetKind.Asset, AssetPath.Parse("/Content/Materials/Stone.omat.json"), IsAutomatic: true), 3);
        var outside = Queue(new(CookTargetKind.Asset, AssetPath.Parse("/Content/MaterialsOld/Wood.omat.json")), 4);
        var reimport = Queue(new(CookTargetKind.Asset, AssetPath.Parse("/Content/Materials/Crate.glb")) { IsReimport = true }, 5);

        _ = coordinator.Runs.Should().HaveCount(3);
        await hold.ReleaseAsync().ConfigureAwait(false);
        _ = (await Task.WhenAll(folder, asset, automatic, outside, reimport).ConfigureAwait(false)).Should().Equal(1, 1, 1, 4, 5);

        Task<int> Queue(CookRunRequest request, int result)
            => coordinator.RunCookAsync(request with { CoalescePending = true }, (_, _) => Task.FromResult(result), this.TestContext.CancellationToken);
    }

    /// <summary>Later saved inputs do not join a running capture and instead get one follow-up run.</summary>
    /// <returns>The asynchronous snapshot isolation regression.</returns>
    [TestMethod]
    public async Task RequestDuringRunningCaptureQueuesDistinctLatestWork()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => release.Task, this.TestContext.CancellationToken);
        var next = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(2), this.TestContext.CancellationToken);
        var samePending = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(3), this.TestContext.CancellationToken);
        _ = coordinator.Runs.Should().HaveCount(2);
        release.SetResult(1);
        _ = (await first.ConfigureAwait(false)).Should().Be(1);
        _ = (await next.ConfigureAwait(false)).Should().Be(2);
        _ = (await samePending.ConfigureAwait(false)).Should().Be(2);
    }

    /// <summary>When all pending observers detach, input capture is cancelled and another request can proceed.</summary>
    /// <returns>The asynchronous final-owner regression.</returns>
    [TestMethod]
    public async Task LastCancelledObserverRemovesPendingWork()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        using var firstOwner = new CancellationTokenSource();
        using var secondOwner = new CancellationTokenSource();
        var hold = WriterHold.Take(coordinator, this.TestContext.CancellationToken);
        var calls = 0;
        var first = coordinator.RunCookAsync(SharedAssetRequest(), Work, firstOwner.Token);
        var second = coordinator.RunCookAsync(SharedAssetRequest(), Work, secondOwner.Token);
        await firstOwner.CancelAsync().ConfigureAwait(false);
        await secondOwner.CancelAsync().ConfigureAwait(false);
        Func<Task> completion = async () => await Task.WhenAll(first, second).ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        await hold.ReleaseAsync().ConfigureAwait(false);
        _ = (await coordinator.RunCookAsync(SharedAssetRequest(), Work, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(1);
        _ = calls.Should().Be(1);

        Task<int> Work(ContentCookOperation operation, CancellationToken token)
        {
            coordinator.VerifyWriter(operation);
            token.ThrowIfCancellationRequested();
            calls++;
            return Task.FromResult(calls);
        }
    }

    /// <summary>A mount change cancels every observer and never shares their run with the project's new mounts.</summary>
    /// <returns>The asynchronous project isolation regression.</returns>
    [TestMethod]
    public async Task SharedPendingWorkCannotCrossProjectLifetime()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        _ = WriterHold.Take(coordinator, this.TestContext.CancellationToken);
        var first = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(1), this.TestContext.CancellationToken);
        var second = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(2), this.TestContext.CancellationToken);
        context.ChangeContext(context.Context with { });
        Func<Task> completion = async () => await Task.WhenAll(first, second).ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = (await coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(3), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(3);
    }

    /// <summary>Running work is cancelled only after both owners detach, and its writer remains held until drain.</summary>
    /// <returns>The asynchronous running-owner regression.</returns>
    [TestMethod]
    public async Task SharedRunningCookStopsOnlyAfterItsLastOwnerDetaches()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        using var firstOwner = new CancellationTokenSource();
        using var secondOwner = new CancellationTokenSource();
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var started = new TaskCompletionSource<CancellationToken>(TaskCreationOptions.RunContinuationsAsynchronously);
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var active = coordinator.RunAsync((_, _) => release.Task, this.TestContext.CancellationToken);
        var first = coordinator.RunCookAsync(SharedAssetRequest(), Work, firstOwner.Token);
        var second = coordinator.RunCookAsync(SharedAssetRequest(), Work, secondOwner.Token);
        release.SetResult(0);
        _ = await active.ConfigureAwait(false);
        var workerToken = await started.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        await firstOwner.CancelAsync().ConfigureAwait(false);
        Func<Task> firstCompletion = async () => await first.ConfigureAwait(false);
        _ = await firstCompletion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = workerToken.IsCancellationRequested.Should().BeFalse();
        await secondOwner.CancelAsync().ConfigureAwait(false);
        Func<Task> secondCompletion = async () => await second.ConfigureAwait(false);
        _ = await secondCompletion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = workerToken.IsCancellationRequested.Should().BeTrue();
        var next = coordinator.RunAsync((_, _) => Task.FromResult(2), this.TestContext.CancellationToken);
        _ = next.IsCompleted.Should().BeFalse();
        drain.SetResult();
        _ = (await next.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(2);

        async Task<int> Work(ContentCookOperation operation, CancellationToken token)
        {
            coordinator.VerifyWriter(operation);
            started.SetResult(token);
            await drain.Task.ConfigureAwait(false);
            token.ThrowIfCancellationRequested();
            return 1;
        }
    }

    /// <summary>The displayed Cancel action stops the shared operation for every observer.</summary>
    /// <returns>The asynchronous run-cancellation regression.</returns>
    [TestMethod]
    public async Task CancellingDisplayedSharedRunCancelsAllObservers()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        var hold = WriterHold.Take(coordinator, this.TestContext.CancellationToken);
        var first = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(1), this.TestContext.CancellationToken);
        var second = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(2), this.TestContext.CancellationToken);
        await coordinator.CancelAsync(coordinator.Runs.Single().OperationId).ConfigureAwait(false);
        Func<Task> completion = async () => await Task.WhenAll(first, second).ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = coordinator.Runs.Single().State.Should().Be(CookRunState.Cancelled);
        await hold.ReleaseAsync().ConfigureAwait(false);
    }

    /// <summary>Different originating contexts cannot bypass the retained delegate's project validation.</summary>
    /// <returns>The asynchronous origin-correlation regression.</returns>
    [TestMethod]
    public async Task SharedScopeRetainsEachOriginatingProjectCheck()
    {
        var context = CreateSession();
        using var coordinator = CreateCoordinator(context);
        var hold = WriterHold.Take(coordinator, this.TestContext.CancellationToken);
        var current = context.Context;
        var old = current with { };
        var first = coordinator.RunCookAsync(SharedAssetRequest() with { OriginContext = current }, (_, _) => Task.FromResult(1), this.TestContext.CancellationToken);
        var stale = coordinator.RunCookAsync(SharedAssetRequest() with { OriginContext = old }, (_, _) => Task.FromException<int>(new OperationCanceledException()), this.TestContext.CancellationToken);
        _ = coordinator.Runs.Should().HaveCount(2);
        await hold.ReleaseAsync().ConfigureAwait(false);
        _ = (await first.ConfigureAwait(false)).Should().Be(1);
        Func<Task> completion = async () => await stale.ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
    }

    private static CookRunRequest SharedAssetRequest()
        => new(CookTargetKind.Asset, AssetPath.Parse("/Content/Shared.omat.json"), IsAutomatic: true) { CoalescePending = true };
}
