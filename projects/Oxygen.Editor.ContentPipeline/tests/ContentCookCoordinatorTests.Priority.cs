// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises priority without preemption and automatic pause across project lifetimes.</summary>
public sealed partial class ContentCookCoordinatorTests
{
    /// <summary>Explicit and demanded work passes queued saves, preserving order within each priority.</summary>
    /// <returns>The asynchronous queue-order regression.</returns>
    [TestMethod]
    public async Task PreviewAndExplicitCooksRunBeforeWaitingSaves()
    {
        var context = CreateContextService();
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
                new(CookTargetKind.Asset, new Uri("asset:///Content/" + name + ".omat.json"), automatic) { IsDemand = demand },
                (_, _) =>
                {
                    order.Add(name);
                    return Task.FromResult(1);
                },
                this.TestContext.CancellationToken);
    }

    /// <summary>Pausing while a writer is active holds queued demand and Save work, but permits explicit work.</summary>
    /// <returns>The asynchronous pause and resume regression.</returns>
    [TestMethod]
    public async Task PauseAppliesToAlreadyQueuedDemandWithoutPreemptingActiveCook()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var active = coordinator.RunAsync((_, _) => release.Task, this.TestContext.CancellationToken);
        var automaticEntered = false;
        var demand = coordinator.RunCookAsync(
            new(CookTargetKind.Asset, new Uri("asset:///Content/Preview.omat.json"), IsAutomatic: true) { IsDemand = true },
            (_, _) =>
            {
                automaticEntered = true;
                return Task.FromResult(1);
            },
            this.TestContext.CancellationToken);
        coordinator.IsAutomaticCookingPaused = true;
        var explicitCook = coordinator.RunCookAsync(new(CookTargetKind.Project, ScopeUri: null), (_, _) => Task.FromResult(2), this.TestContext.CancellationToken);
        _ = active.IsCompleted.Should().BeFalse();
        release.SetResult(0);
        _ = await active.ConfigureAwait(false);
        _ = (await explicitCook.ConfigureAwait(false)).Should().Be(2);
        _ = automaticEntered.Should().BeFalse();
        coordinator.IsAutomaticCookingPaused = false;
        _ = (await demand.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(1);
    }

    /// <summary>Removing a cancelled priority waiter allows the next save to acquire the writer.</summary>
    /// <returns>The asynchronous cancellation regression.</returns>
    [TestMethod]
    public async Task CancelledDemandDoesNotConsumeWriterOrBlockQueuedSave()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        using var cancelled = new CancellationTokenSource();
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var active = coordinator.RunAsync((_, _) => release.Task, this.TestContext.CancellationToken);
        var save = coordinator.RunCookAsync(
            new(CookTargetKind.Asset, new Uri("asset:///Content/Saved.omat.json"), IsAutomatic: true),
            (_, _) => Task.FromResult(1),
            this.TestContext.CancellationToken);
        var entered = false;
        var demand = coordinator.RunCookAsync(
            new(CookTargetKind.Asset, new Uri("asset:///Content/Preview.omat.json"), IsAutomatic: true) { IsDemand = true },
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

    /// <summary>Automatic pause belongs to the old project and cannot strand a new project's requests.</summary>
    /// <returns>The asynchronous project-lifetime regression.</returns>
    [TestMethod]
    public async Task SwitchingProjectClearsPauseAndCancelsPausedRequests()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        coordinator.IsAutomaticCookingPaused = true;
        var request = new CookRunRequest(CookTargetKind.Asset, new Uri("asset:///Content/Preview.omat.json"), IsAutomatic: true) { IsDemand = true };
        var old = coordinator.RunCookAsync(request, (_, _) => Task.FromResult(1), this.TestContext.CancellationToken);
        context.Activate(CreateContext());
        _ = coordinator.IsAutomaticCookingPaused.Should().BeFalse();
        Func<Task> completion = async () => await old.ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = (await coordinator.RunCookAsync(request, (_, _) => Task.FromResult(2), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(2);
    }
}
