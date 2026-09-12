// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

// Fake readers own no I/O. Their disposal must come from EngineService so the
// assertions detect early release, missed release, and duplicate release.
#pragma warning disable CA2000

public sealed partial class EngineServiceShutdownTests
{
    [TestMethod]
    public async Task ContentRefresh_ReleasesPreviousReadersOnlyAfterNativeAcknowledgement()
    {
        var native = new FakeEngineSession();
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        var previous = new ContentReader();
        var current = new ContentReader();
        await service.RefreshProjectCookedRootsAsync(["old"], previous).ConfigureAwait(false);
        var acknowledgement = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = Mock.Get(native.Commands).Setup(value => value.ReplaceCookedRootsAsync(It.IsAny<IReadOnlyList<string>>())).Returns(acknowledgement.Task);
        var refresh = service.RefreshProjectCookedRootsAsync(["new"], current, keepPaused: true);
        try
        {
            _ = refresh.IsCompleted.Should().BeFalse();
            _ = previous.DisposeCount.Should().Be(0);
            _ = current.DisposeCount.Should().Be(0);
        }
        finally
        {
            acknowledgement.SetResult();
        }

        await refresh.ConfigureAwait(false);
        _ = previous.DisposeCount.Should().Be(1);
        _ = current.DisposeCount.Should().Be(0);
        Mock.Get(native.Commands).Verify(value => value.SetCookedContentPausedAsync(paused: false), Times.Once());
        await service.SuspendCookedContentAsync().ConfigureAwait(false);
        _ = current.DisposeCount.Should().Be(1);
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task FailedContentRefresh_RetainsBothReadersUntilNativeSuspension(bool canceled)
    {
        var native = new FakeEngineSession();
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        var previous = new ContentReader();
        var current = new ContentReader();
        await service.RefreshProjectCookedRootsAsync(["old"], previous).ConfigureAwait(false);
        var failed = canceled ? Task.FromCanceled(new CancellationToken(canceled: true)) : Task.FromException(new InvalidOperationException("Refresh failed"));
        _ = Mock.Get(native.Commands).Setup(value => value.ReplaceCookedRootsAsync(It.IsAny<IReadOnlyList<string>>())).Returns(failed);
        var refresh = () => service.RefreshProjectCookedRootsAsync(["new"], current);
        _ = await refresh.Should().ThrowAsync<Exception>().ConfigureAwait(false);
        _ = previous.DisposeCount.Should().Be(0);
        _ = current.DisposeCount.Should().Be(0);
        var suspension = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = Mock.Get(native.Commands).Setup(value => value.SetCookedContentPausedAsync(paused: true)).Returns(suspension.Task);
        var suspend = service.SuspendCookedContentAsync();
        try
        {
            _ = suspend.IsCompleted.Should().BeFalse();
            _ = previous.DisposeCount.Should().Be(0);
            _ = current.DisposeCount.Should().Be(0);
        }
        finally
        {
            suspension.SetResult();
        }

        await suspend.ConfigureAwait(false);
        _ = previous.DisposeCount.Should().Be(1);
        _ = current.DisposeCount.Should().Be(1);
    }

    [TestMethod]
    public async Task ContentRefreshWithoutRuntime_DisposesUnclaimedReader()
    {
        var service = Create(new FakeEngineSession());
        await using var lifetime = service.ConfigureAwait(false);
        var reader = new ContentReader();
        _ = await ThrowsAsync<InvalidOperationException>(() => service.RefreshProjectCookedRootsAsync(["root"], reader)).ConfigureAwait(false);
        _ = reader.DisposeCount.Should().Be(1);
    }

    [TestMethod]
    public async Task Shutdown_WaitsForContentRefreshBeforeReleasingReaders()
    {
        var native = new FakeEngineSession();
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        var reader = new ContentReader();
        var acknowledgement = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = Mock.Get(native.Commands).Setup(value => value.ReplaceCookedRootsAsync(It.IsAny<IReadOnlyList<string>>())).Returns(acknowledgement.Task);
        var refresh = service.RefreshProjectCookedRootsAsync(["root"], reader);
        var shutdown = service.ShutdownAsync().AsTask();
        try
        {
            _ = shutdown.IsCompleted.Should().BeFalse();
            _ = reader.DisposeCount.Should().Be(0);
            _ = native.Calls.Should().NotContain("Stop");
        }
        finally
        {
            acknowledgement.SetResult();
        }

        await Task.WhenAll(refresh, shutdown).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reader.DisposeCount.Should().Be(1);
        _ = native.HasContext.Should().BeFalse();
    }

    [TestMethod]
    public async Task LoopExitDuringContentRefresh_RetainsReaderThroughFailedNativeTeardown()
    {
        var native = new FakeEngineSession();
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        var reader = new ContentReader();
        var acknowledgement = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = Mock.Get(native.Commands).Setup(value => value.ReplaceCookedRootsAsync(It.IsAny<IReadOnlyList<string>>())).Returns(acknowledgement.Task);
        var refresh = service.RefreshProjectCookedRootsAsync(["root"], reader);
        native.Loop.SetResult();
        _ = await ThrowsAsync<InvalidOperationException>(() => refresh.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken)).ConfigureAwait(false);
        acknowledgement.SetCanceled(this.TestContext.CancellationToken);
        _ = reader.DisposeCount.Should().Be(0);
        native.FailAt = "Destroy context";
        _ = await ThrowsAsync<AggregateException>(() => service.ShutdownAsync().AsTask()).ConfigureAwait(false);
        _ = reader.DisposeCount.Should().Be(0);
        native.FailAt = null;
        await service.ShutdownAsync().ConfigureAwait(false);
        _ = reader.DisposeCount.Should().Be(1);
    }

    [TestMethod]
    public async Task ReaderCleanupFailure_RetainsOwnershipForRetryAndReleasesOtherReaders()
    {
        var native = new FakeEngineSession();
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        var failed = new ContentReader { Fail = true };
        var other = new ContentReader();
        await service.RefreshProjectCookedRootsAsync(["old"], failed).ConfigureAwait(false);
        _ = Mock.Get(native.Commands).Setup(value => value.ReplaceCookedRootsAsync(It.IsAny<IReadOnlyList<string>>())).Returns(Task.FromException(new InvalidOperationException("Refresh failed")));
        _ = await ThrowsAsync<InvalidOperationException>(() => service.RefreshProjectCookedRootsAsync(["new"], other)).ConfigureAwait(false);
        _ = await ThrowsAsync<AggregateException>(() => service.ShutdownAsync().AsTask()).ConfigureAwait(false);
        _ = native.HasContext.Should().BeFalse();
        _ = service.State.Should().Be(EngineServiceState.Faulted);
        _ = failed.DisposeCount.Should().Be(0);
        _ = other.DisposeCount.Should().Be(1);
        failed.Fail = false;
        await service.ShutdownAsync().ConfigureAwait(false);
        _ = failed.DisposeCount.Should().Be(1);
        _ = other.DisposeCount.Should().Be(1);
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
    }

    private sealed partial class ContentReader : IDisposable
    {
        public int DisposeCount { get; private set; }

        public bool Fail { get; set; }

        public void Dispose()
        {
            if (this.Fail)
            {
                throw new IOException("Reader cleanup failed");
            }

            this.DisposeCount++;
        }
    }
}

#pragma warning restore CA2000
