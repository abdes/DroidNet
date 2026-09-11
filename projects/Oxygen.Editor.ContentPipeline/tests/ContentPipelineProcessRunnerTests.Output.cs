// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using AwesomeAssertions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies live output delivery without compromising owned worker drain.</summary>
public sealed partial class ContentPipelineProcessRunnerTests
{
    /// <summary>Delivers both native streams while the worker is still running and stops only after drain.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_ReportsLinesBeforeWorkerExits()
    {
        var scope = new WorkerScope();
        await using var lifetime = scope.ConfigureAwait(false);
        var output = new CapturedOutput();
        var operation = scope.Run(["write", scope.Root], output);
        await WaitUntilAsync(() => output.Lines.Any(static line => line.IsStandardError)
            && output.Lines.Any(static line => !line.IsStandardError)).ConfigureAwait(false);
        _ = operation.IsCompleted.Should().BeFalse();
        await scope.Cancellation.CancelAsync().ConfigureAwait(false);
        Func<Task> cancelled = async () => _ = await operation.ConfigureAwait(false);
        _ = await cancelled.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = output.Lines.Should().OnlyContain(static line => line.Text == "root");
    }

    /// <summary>Preserves exact captured output and delivers unterminated lines at EOF.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_ReportsUnterminatedOutputWithoutChangingCapturedBytes()
    {
        var scope = new WorkerScope();
        await using var lifetime = scope.ConfigureAwait(false);
        var output = new CapturedOutput();
        var result = await scope.Run(["flood"], output).ConfigureAwait(false);
        _ = result.StandardOutput.Should().Be(new string('x', 300_000));
        _ = result.StandardError.Should().Be(new string('y', 300_000));
        _ = output.Lines.Should().HaveCount(2);
        _ = output.Lines.Single(static line => !line.IsStandardError).Text.Should().Be(result.StandardOutput);
        _ = output.Lines.Single(static line => line.IsStandardError).Text.Should().Be(result.StandardError);
    }

    private sealed class CapturedOutput : IProgress<ContentPipelineProcessOutput>
    {
        internal ConcurrentQueue<ContentPipelineProcessOutput> Lines { get; } = new();

        public void Report(ContentPipelineProcessOutput value) => this.Lines.Enqueue(value);
    }
}
