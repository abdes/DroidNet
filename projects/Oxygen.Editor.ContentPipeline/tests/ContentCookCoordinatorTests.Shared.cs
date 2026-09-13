// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks shared pending scopes, independent observers, and immutable running captures.</summary>
public sealed partial class ContentCookCoordinatorTests
{
    /// <summary>Two pending callers get one run; cancelling one leaves the other caller's work intact.</summary>
    /// <returns>The asynchronous shared ownership regression.</returns>
    [TestMethod]
    public async Task PendingCookIsSharedAndOneCancelledObserverDoesNotStopIt()
    {
        var context = CreateContextService();
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

    /// <summary>An explicit caller promotes and resumes an equivalent paused automatic scope.</summary>
    /// <returns>The asynchronous promotion regression.</returns>
    [TestMethod]
    public async Task ExplicitRequestPromotesSharedPausedSaveAndRevealsItsRun()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        coordinator.IsAutomaticCookingPaused = true;
        var revealed = false;
        coordinator.RunChanged += (_, args) => revealed |= args.Reveal;
        var save = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(1), this.TestContext.CancellationToken);
        var explicitCook = coordinator.RunCookAsync(SharedAssetRequest() with { IsAutomatic = false }, (_, _) => Task.FromResult(2), this.TestContext.CancellationToken);
        _ = (await save.ConfigureAwait(false)).Should().Be(1);
        _ = (await explicitCook.ConfigureAwait(false)).Should().Be(1);
        _ = coordinator.Runs.Should().ContainSingle().Which.Request.IsAutomatic.Should().BeFalse();
        _ = revealed.Should().BeTrue();
    }

    /// <summary>Later saved inputs do not join a running capture and instead get one follow-up run.</summary>
    /// <returns>The asynchronous snapshot isolation regression.</returns>
    [TestMethod]
    public async Task RequestDuringRunningCaptureQueuesDistinctLatestWork()
    {
        var context = CreateContextService();
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
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        using var firstOwner = new CancellationTokenSource();
        using var secondOwner = new CancellationTokenSource();
        coordinator.IsAutomaticCookingPaused = true;
        var calls = 0;
        var first = coordinator.RunCookAsync(SharedAssetRequest(), Work, firstOwner.Token);
        var second = coordinator.RunCookAsync(SharedAssetRequest(), Work, secondOwner.Token);
        await firstOwner.CancelAsync().ConfigureAwait(false);
        await secondOwner.CancelAsync().ConfigureAwait(false);
        Func<Task> completion = async () => await Task.WhenAll(first, second).ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        coordinator.IsAutomaticCookingPaused = false;
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

    /// <summary>Project replacement cancels every observer and never shares their run with the new project.</summary>
    /// <returns>The asynchronous project isolation regression.</returns>
    [TestMethod]
    public async Task SharedPendingWorkCannotCrossProjectLifetime()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        coordinator.IsAutomaticCookingPaused = true;
        var first = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(1), this.TestContext.CancellationToken);
        var second = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(2), this.TestContext.CancellationToken);
        context.Activate(CreateContext());
        Func<Task> completion = async () => await Task.WhenAll(first, second).ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = (await coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(3), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(3);
    }

    /// <summary>Running work is cancelled only after both owners detach, and its writer remains held until drain.</summary>
    /// <returns>The asynchronous running-owner regression.</returns>
    [TestMethod]
    public async Task SharedRunningCookStopsOnlyAfterItsLastOwnerDetaches()
    {
        var context = CreateContextService();
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
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        coordinator.IsAutomaticCookingPaused = true;
        var first = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(1), this.TestContext.CancellationToken);
        var second = coordinator.RunCookAsync(SharedAssetRequest(), (_, _) => Task.FromResult(2), this.TestContext.CancellationToken);
        await coordinator.CancelAsync(coordinator.Runs.Single().OperationId).ConfigureAwait(false);
        Func<Task> completion = async () => await Task.WhenAll(first, second).ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = coordinator.Runs.Single().State.Should().Be(CookRunState.Cancelled);
    }

    /// <summary>Different originating contexts cannot bypass the retained delegate's project validation.</summary>
    /// <returns>The asynchronous origin-correlation regression.</returns>
    [TestMethod]
    public async Task SharedScopeRetainsEachOriginatingProjectCheck()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        coordinator.IsAutomaticCookingPaused = true;
        var current = context.ActiveProject!;
        var old = current with { };
        var first = coordinator.RunCookAsync(SharedAssetRequest() with { OriginContext = current }, (_, _) => Task.FromResult(1), this.TestContext.CancellationToken);
        var stale = coordinator.RunCookAsync(SharedAssetRequest() with { OriginContext = old }, (_, _) => Task.FromException<int>(new OperationCanceledException()), this.TestContext.CancellationToken);
        _ = coordinator.Runs.Should().HaveCount(2);
        coordinator.IsAutomaticCookingPaused = false;
        _ = (await first.ConfigureAwait(false)).Should().Be(1);
        Func<Task> completion = async () => await stale.ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
    }

    private static CookRunRequest SharedAssetRequest()
        => new(CookTargetKind.Asset, new Uri("asset:///Content/Shared.omat.json"), IsAutomatic: true) { CoalescePending = true };
}
