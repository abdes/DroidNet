// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Processes;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies native worker ownership and cancellation.</summary>
public sealed partial class ContentPipelineProcessRunnerTests
{
    /// <summary>Keeps ownership until both readers finish after termination.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_CancellationWaitsForBothReaders()
    {
        using var cancellation = new CancellationTokenSource();
        using var worker = new ControlledWorker();
        worker.OnTerminate = () =>
        {
            worker.ExitSource.SetResult(130);
            return true;
        };
        var operation = new ContentPipelineProcessRunner(_ => worker).RunAsync(CreateControlledRequest(), cancellation.Token);

        await cancellation.CancelAsync().ConfigureAwait(false);
        await worker.TerminationRequested.Task.WaitAsync(TimeSpan.FromSeconds(5), CancellationToken.None).ConfigureAwait(false);
        _ = operation.IsCompleted.Should().BeFalse();
        _ = worker.IsDisposed.Should().BeFalse();
        worker.OutputSource.SetResult("final output");
        _ = operation.IsCompleted.Should().BeFalse();
        worker.ErrorSource.SetResult("final error");

        var run = async () => await operation.ConfigureAwait(false);
        _ = await run.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = worker.IsDisposed.Should().BeTrue();
    }

    /// <summary>Stops writing on reader failure and observes both reader outcomes.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_ReaderFailureStopsWriterAndObservesOtherReader()
    {
        using var worker = new ControlledWorker();
        worker.OnTerminate = () =>
        {
            worker.ExitSource.SetResult(1);
            return true;
        };
        var operation = new ContentPipelineProcessRunner(_ => worker).RunAsync(CreateControlledRequest(), CancellationToken.None);
        worker.OutputSource.SetException(new IOException("output reader failed"));

        await worker.TerminationRequested.Task.WaitAsync(TimeSpan.FromSeconds(5), CancellationToken.None).ConfigureAwait(false);
        _ = operation.IsCompleted.Should().BeFalse();
        worker.ErrorSource.SetException(new InvalidDataException("error reader failed"));
        var run = async () => await operation.ConfigureAwait(false);
        _ = await run.Should().ThrowAsync<IOException>().WithMessage("output reader failed").ConfigureAwait(false);
        _ = worker.IsDisposed.Should().BeTrue();
    }

    /// <summary>Transfers the drain lifetime when native termination fails.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_TerminationFailureRetainsOwnershipUntilTreeAndReadersFinish()
    {
        using var cancellation = new CancellationTokenSource();
        using var worker = new ControlledWorker { OnTerminate = () => throw new Win32Exception(5) };
        var operation = new ContentPipelineProcessRunner(_ => worker).RunAsync(CreateControlledRequest(), cancellation.Token);
        await cancellation.CancelAsync().ConfigureAwait(false);
        var run = async () => await operation.ConfigureAwait(false);
        var failure = await run.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);

        _ = failure.Which.DrainCompletion.IsCompleted.Should().BeFalse();
        _ = worker.IsDisposed.Should().BeFalse();
        worker.Complete(0);
        await failure.Which.DrainCompletion.WaitAsync(TimeSpan.FromSeconds(5), CancellationToken.None).ConfigureAwait(false);
        _ = worker.IsDisposed.Should().BeTrue();
    }

    /// <summary>Preserves a natural terminal result that wins the cancellation race.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_NaturalExitWinsConcurrentCancellation()
    {
        using var cancellation = new CancellationTokenSource();
        using var worker = new ControlledWorker();
        var runner = new ContentPipelineProcessRunner(_ =>
        {
            worker.Complete(7);
            cancellation.Cancel();
            return worker;
        });

        var result = await runner.RunAsync(CreateControlledRequest(), cancellation.Token).ConfigureAwait(false);
        _ = result.ExitCode.Should().Be(7);
        _ = worker.TerminationRequested.Task.IsCompleted.Should().BeFalse();
    }

    /// <summary>Limits termination to the operation-owned job.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_CancellingOneJobDoesNotStopAnotherWorker()
    {
        var first = new WorkerScope();
        await using var firstLifetime = first.ConfigureAwait(false);
        var second = new WorkerScope();
        await using var secondLifetime = second.ConfigureAwait(false);
        var cancelledOperation = first.Run(["write", first.Root]);
        var unaffectedOperation = second.Run(["write", second.Root]);
        var firstWrites = Path.Combine(first.Root, "root.writes");
        var secondWrites = Path.Combine(second.Root, "root.writes");
        await WaitUntilAsync(() => File.Exists(firstWrites) && File.Exists(secondWrites)).ConfigureAwait(false);
        await first.Cancellation.CancelAsync().ConfigureAwait(false);
        var cancel = async () => await cancelledOperation.ConfigureAwait(false);
        _ = await cancel.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        var length = new FileInfo(secondWrites).Length;
        await WaitUntilAsync(() => new FileInfo(secondWrites).Length > length).ConfigureAwait(false);
        _ = unaffectedOperation.IsCompleted.Should().BeFalse();
    }

    private static ContentPipelineProcessRequest CreateControlledRequest()
        => new("controlled-worker.exe", [], Path.GetTempPath());

    private sealed partial class ControlledWorker : IContentPipelineWorker
    {
        public Task<int> Exit => this.ExitSource.Task;

        public Task<string> StandardOutput => this.OutputSource.Task;

        public Task<string> StandardError => this.ErrorSource.Task;

        internal TaskCompletionSource<int> ExitSource { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        internal TaskCompletionSource<string> OutputSource { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        internal TaskCompletionSource<string> ErrorSource { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        internal TaskCompletionSource TerminationRequested { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        internal Func<bool> OnTerminate { get; set; } = static () => false;

        internal bool IsDisposed { get; private set; }

        public bool Terminate()
        {
            _ = this.TerminationRequested.TrySetResult();
            return this.OnTerminate();
        }

        public void Dispose() => this.IsDisposed = true;

        internal void Complete(int exitCode)
        {
            this.ExitSource.SetResult(exitCode);
            this.OutputSource.SetResult(string.Empty);
            this.ErrorSource.SetResult(string.Empty);
        }
    }
}
