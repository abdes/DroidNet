// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks attention when a user request needs input or its queue finishes.</summary>
public sealed partial class ContentCookCoordinatorTests
{
    /// <summary>An explicit request reveals its recovery action when it becomes blocked; automatic work remains quiet.</summary>
    /// <param name="automatic">Whether the request is background work.</param>
    /// <returns>The asynchronous attention test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task NeedsSaveRevealsOnlyUserInitiatedWork(bool automatic)
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        var needsSave = new TaskCompletionSource<CookRunChangedEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
        coordinator.RunChanged += (_, change) =>
        {
            if (change.Run.State == CookRunState.NeedsSave)
            {
                _ = needsSave.TrySetResult(change);
            }
        };
        var document = new Snapshots.CookDocumentState(Guid.NewGuid(), Path.Combine(Path.GetTempPath(), "Main.oscene.json"), "Main", Revision: 2, SavedRevision: 1, IsDirty: true, SavedContentHash: "saved");
        var work = coordinator.RunCookAsync<int>(new(CookTargetKind.Project, ScopeUri: null, IsAutomatic: automatic), (_, _) => throw new CookInputsNeedSaveException([document]), CancellationToken.None);
        var blocked = await needsSave.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = blocked.Reveal.Should().Be(!automatic);
        await coordinator.CancelAsync(blocked.Run.OperationId).ConfigureAwait(false);
        Func<Task> cancelled = () => work;
        _ = await cancelled.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
    }

    /// <summary>When a user's queue drains, a failed run takes precedence over the last successful run.</summary>
    /// <returns>The asynchronous queue outcome test.</returns>
    [TestMethod]
    public async Task CompletedQueueRevealsProblemsBeforeSuccessfulResults()
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        var events = new ConcurrentQueue<CookRunChangedEventArgs>();
        coordinator.RunChanged += (_, change) => events.Enqueue(change);
        var finish = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = coordinator.RunCookAsync(new(CookTargetKind.Project, ScopeUri: null), (_, _) => finish.Task, CancellationToken.None);
        var firstId = coordinator.Runs.Single().OperationId;
        var second = coordinator.RunCookAsync(new(CookTargetKind.Asset, new Uri("asset:///Content/Other.omat.json")), (_, _) => Task.FromResult(2), CancellationToken.None);
        events.Clear();
        finish.SetException(new IOException("First cook failed."));
        Func<Task> failed = () => first;
        _ = await failed.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        _ = await second.ConfigureAwait(false);
        _ = events.Where(static change => change.Reveal).Should().ContainSingle().Which.Run.OperationId.Should().Be(firstId);
        _ = coordinator.Runs.Should().OnlyContain(static run => run.IsCompleted);
    }
}
