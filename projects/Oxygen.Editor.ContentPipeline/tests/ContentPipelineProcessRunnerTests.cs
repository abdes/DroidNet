// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies native worker ownership and cancellation.</summary>
[TestClass]
[SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class ContentPipelineProcessRunnerTests
{
    /// <summary>Preserves empty, quoted, Unicode, and shell-looking arguments as literal tokens.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_PreservesArgumentTokensThroughNativeLaunch()
    {
        var scope = new WorkerScope();
        await using var scopeLifetime = scope.ConfigureAwait(false);
        string[] arguments = [string.Empty, "two words", "quoted\"value", "ends in \\", "\\\"", "α scene", "a&b|c", "$(literal)"];
        var result = await scope.Run(["echo", .. arguments]).WaitAsync(TimeSpan.FromSeconds(10), CancellationToken.None).ConfigureAwait(false);
        _ = JsonSerializer.Deserialize<string[]>(result.StandardOutput).Should().Equal(arguments);
        _ = result.ExitCode.Should().Be(0);
    }

    /// <summary>Captures both streams without a pipe-buffer deadlock.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_DrainsLargeOutputOnBothStreams()
    {
        var scope = new WorkerScope();
        await using var scopeLifetime = scope.ConfigureAwait(false);
        var result = await scope.Run(["flood"]).WaitAsync(TimeSpan.FromSeconds(10), CancellationToken.None).ConfigureAwait(false);
        _ = result.StandardOutput.Should().Be(new string('x', 300_000));
        _ = result.StandardError.Should().Be(new string('y', 300_000));
        _ = result.ExitCode.Should().Be(7);
    }

    /// <summary>Preserves the exit code when the worker finishes during startup.</summary>
    /// <param name="exitCode">The root worker exit code.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow("0")]
    [DataRow("7")]
    [DataRow("97")]
    public async Task RunAsync_ObservesImmediateExit(string exitCode)
    {
        var scope = new WorkerScope();
        await using var scopeLifetime = scope.ConfigureAwait(false);
        var result = await scope.Run(["exit", exitCode]).WaitAsync(TimeSpan.FromSeconds(10), CancellationToken.None).ConfigureAwait(false);
        _ = result.ExitCode.ToString(System.Globalization.CultureInfo.InvariantCulture).Should().Be(exitCode);
    }

    /// <summary>Stops the root and descendants before acknowledging cancellation.</summary>
    /// <param name="mode">The worker or descendant mode.</param>
    /// <param name="observedWriter">The writer whose termination is observed.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow("write", "root")]
    [DataRow("tree", "child")]
    [DataRow("orphan", "child")]
    public async Task RunAsync_CancelStopsOwnedWritersBeforeReturning(string mode, string observedWriter)
    {
        var scope = new WorkerScope();
        await using var scopeLifetime = scope.ConfigureAwait(false);
        var operation = scope.Run([mode, scope.Root]);
        var writes = Path.Combine(scope.Root, observedWriter + ".writes");
        await WaitUntilAsync(() => File.Exists(writes) && new FileInfo(writes).Length > 0).ConfigureAwait(false);
        _ = operation.IsCompleted.Should().BeFalse("a running descendant still belongs to the operation even if its parent exited");
        await scope.Cancellation.CancelAsync().ConfigureAwait(false);
        var cancelled = async () => await operation.WaitAsync(TimeSpan.FromSeconds(10), CancellationToken.None).ConfigureAwait(false);
        _ = await cancelled.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);

        var length = new FileInfo(writes).Length;
        await Task.Delay(120, CancellationToken.None).ConfigureAwait(false);
        _ = new FileInfo(writes).Length.Should().Be(length, "successful cancellation means all writes have stopped");
        foreach (var pidPath in Directory.EnumerateFiles(scope.Root, "*.pid"))
        {
            var pid = int.Parse(await File.ReadAllTextAsync(pidPath, CancellationToken.None).ConfigureAwait(false), System.Globalization.CultureInfo.InvariantCulture);
            _ = IsRunning(pid).Should().BeFalse();
        }
    }

    /// <summary>Reports launch failure without starting a writer.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_StartupFailureDoesNotLeaveAWorker()
    {
        var scope = new WorkerScope();
        await using var scopeLifetime = scope.ConfigureAwait(false);
        var run = async () => await new ContentPipelineProcessRunner().RunAsync(
            new(Path.Combine(scope.Root, "missing.exe"), [], scope.Root), CancellationToken.None).ConfigureAwait(false);
        _ = await run.Should().ThrowAsync<Win32Exception>().ConfigureAwait(false);
        _ = Directory.EnumerateFiles(scope.Root).Should().BeEmpty();
    }

    /// <summary>Rejects cancelled requests before worker creation.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_AlreadyCancelledDoesNotLaunch()
    {
        var scope = new WorkerScope();
        await using var scopeLifetime = scope.ConfigureAwait(false);
        await scope.Cancellation.CancelAsync().ConfigureAwait(false);
        var run = async () => await scope.Run(["write", scope.Root]).ConfigureAwait(false);
        _ = await run.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = Directory.EnumerateFiles(scope.Root).Should().BeEmpty();
    }

    /// <summary>Exercises cancellation racing a real worker's natural exit.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_RacingCancellationAndExitReturnsACompleteTerminalResult()
    {
        int[] delays = [1, 5, 25, 50];
        foreach (var delay in delays)
        {
            var scope = new WorkerScope();
            await using var scopeLifetime = scope.ConfigureAwait(false);
            var operation = scope.Run(["exit", "0"]);
            scope.Cancellation.CancelAfter(delay);
            try
            {
                var result = await operation.WaitAsync(TimeSpan.FromSeconds(10), CancellationToken.None).ConfigureAwait(false);
                _ = result.ExitCode.Should().Be(0);
                _ = result.StandardOutput.Should().BeEmpty();
                _ = result.StandardError.Should().BeEmpty();
            }
            catch (OperationCanceledException)
            {
                _ = scope.Cancellation.IsCancellationRequested.Should().BeTrue();
            }
        }
    }

    private static bool IsRunning(int pid)
    {
        try
        {
            using var process = Process.GetProcessById(pid);
            return !process.HasExited;
        }
        catch (ArgumentException)
        {
            return false;
        }
    }

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        while (!condition())
        {
            await Task.Delay(20, deadline.Token).ConfigureAwait(false);
        }
    }

    private sealed class WorkerScope : IAsyncDisposable
    {
        private Task<ContentPipelineProcessResult>? operation;

        internal WorkerScope()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen content worker tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
        }

        internal string Root { get; }

        internal CancellationTokenSource Cancellation { get; } = new(TimeSpan.FromSeconds(20));

        public async ValueTask DisposeAsync()
        {
            await this.Cancellation.CancelAsync().ConfigureAwait(false);
            if (this.operation is { } operation)
            {
                try
                {
                    _ = await operation.WaitAsync(TimeSpan.FromSeconds(10), CancellationToken.None).ConfigureAwait(false);
                }
                catch (Exception ex) when (ex is OperationCanceledException or Win32Exception)
                {
                }
            }

            this.Cancellation.Dispose();
            Directory.Delete(this.Root, recursive: true);
        }

        internal Task<ContentPipelineProcessResult> Run(IReadOnlyList<string> arguments, IProgress<ContentPipelineProcessOutput>? output = null)
        {
            var executable = Path.Combine(AppContext.BaseDirectory, "WorkerProbe", "Oxygen.Editor.ContentPipeline.WorkerProbe.exe");
            this.operation = new ContentPipelineProcessRunner().RunAsync(new(executable, arguments, this.Root, output), this.Cancellation.Token);
            return this.operation;
        }
    }
}
