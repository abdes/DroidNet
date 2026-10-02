// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>Distinguishes queued native mutations from acknowledged content availability.</summary>
public sealed partial class EngineServiceShutdownTests
{
    /// <summary>Neither a queued root replacement nor paused publication can claim mounted availability.</summary>
    /// <returns>The asynchronous native-acknowledgement regression.</returns>
    [TestMethod]
    public async Task ContentStatusWaitsForMountAndResumeAcknowledgements()
    {
        var native = new FakeEngineSession();
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        var run = service.WorldCommands.RunId;
        _ = service.ContentStatus.State.Should().Be(RuntimeContentState.Unmounted);
        var acknowledgement = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var updating = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        service.ContentStatusChanged += (_, args) =>
        {
            if (args.Snapshot.State == RuntimeContentState.Updating)
            {
                _ = updating.TrySetResult();
            }
        };
        var transport = Mock.Get(native.Commands);
        _ = transport.Setup(value => value.ReplaceCookedRootsAsync(It.IsAny<IReadOnlyList<RuntimeCookedRoot>>())).Returns(acknowledgement.Task);
        var roots = new[] { new RuntimeCookedRoot("published", "Content") };
        var refresh = service.RefreshProjectCookedRootsAsync(roots, keepPaused: true);
        try
        {
            roots[0] = new("later mutation", "Content");
            await updating.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = refresh.IsCompleted.Should().BeFalse();
            _ = service.ContentStatus.State.Should().Be(RuntimeContentState.Updating);
            _ = service.ContentStatus.Roots.Should().BeEmpty();
        }
        finally
        {
            acknowledgement.SetResult();
        }

        await refresh.ConfigureAwait(false);
        _ = service.ContentStatus.Bindings.Single().ProjectMount.Should().Be("Content");
        _ = service.ContentStatus.Roots.Should().Equal("published");
        _ = service.ContentStatus.State.Should().Be(RuntimeContentState.Updating);
        await service.ResumeCookedContentAsync().ConfigureAwait(false);
        _ = service.ContentStatus.State.Should().Be(RuntimeContentState.Mounted);
        _ = service.ContentStatus.RunId.Should().Be(run);
        transport.Verify(value => value.ReplaceCookedRootsAsync(It.Is<IReadOnlyList<RuntimeCookedRoot>>(paths => paths.Count == 1 && string.Equals(paths[0].Path, "published", StringComparison.Ordinal))), Times.Once());
    }

    /// <summary>A failed replacement invalidates native availability until a real restoration succeeds.</summary>
    /// <returns>The asynchronous rollback-state regression.</returns>
    [TestMethod]
    public async Task FailedContentStatusRecoversOnlyAfterAcknowledgedReplacement()
    {
        var native = new FakeEngineSession();
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        await service.RefreshProjectCookedRootsAsync([new("previous", "Content")]).ConfigureAwait(false);
        var transport = Mock.Get(native.Commands);
        _ = transport.Setup(value => value.ReplaceCookedRootsAsync(It.IsAny<IReadOnlyList<RuntimeCookedRoot>>())).Returns(Task.FromException(new InvalidOperationException("Native mount rejected")));
        _ = await ThrowsAsync<InvalidOperationException>(() => service.RefreshProjectCookedRootsAsync([new("replacement", "Content")])).ConfigureAwait(false);
        var failed = service.ContentStatus;
        _ = failed.State.Should().Be(RuntimeContentState.Failed);
        _ = failed.Roots.Should().Equal("previous");
        _ = failed.Reason.Should().Be("Native mount rejected");
        _ = transport.Setup(value => value.ReplaceCookedRootsAsync(It.IsAny<IReadOnlyList<RuntimeCookedRoot>>())).Returns(Task.CompletedTask);
        await service.RefreshProjectCookedRootsAsync([new("previous", "Content")]).ConfigureAwait(false);
        _ = service.ContentStatus.State.Should().Be(RuntimeContentState.Mounted);
        _ = service.ContentStatus.Roots.Should().Equal("previous");
        _ = service.ContentStatus.Revision.Should().BeGreaterThan(failed.Revision);
        _ = service.ContentStatus.Reason.Should().BeNull();
    }

    /// <summary>Loop exit clears the getter's availability before the asynchronous supervisor runs.</summary>
    /// <returns>The asynchronous stopped-runtime regression.</returns>
    [TestMethod]
    public async Task ContentStatusCannotRemainMountedAfterLoopExit()
    {
        var native = new FakeEngineSession();
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        await service.RefreshProjectCookedRootsAsync([new("published", "Content")]).ConfigureAwait(false);
        native.Loop.SetResult();
        _ = service.ContentStatus.State.Should().Be(RuntimeContentState.Unavailable);
        _ = service.ContentStatus.Roots.Should().BeEmpty();
    }

    /// <summary>Throwing subscribers cannot stop native content operations or suppress other subscribers.</summary>
    /// <returns>The asynchronous notification-isolation regression.</returns>
    [TestMethod]
    public async Task ContentNotificationsStayOrderedAndIsolateSubscribers()
    {
        var service = await this.StartAsync(new FakeEngineSession()).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        var revisions = new ConcurrentQueue<long>();
        var mounted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        service.ContentStatusChanged += (_, _) => throw new InvalidOperationException("Broken observer");
        service.ContentStatusChanged += (_, args) =>
        {
            revisions.Enqueue(args.Snapshot.Revision);
            if (args.Snapshot.State == RuntimeContentState.Mounted)
            {
                _ = mounted.TrySetResult();
            }
        };
        await service.RefreshProjectCookedRootsAsync([new("published", "Content")]).ConfigureAwait(false);
        await mounted.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = revisions.Should().BeInAscendingOrder().And.OnlyHaveUniqueItems();
        _ = service.ContentStatus.State.Should().Be(RuntimeContentState.Mounted);
    }
}
