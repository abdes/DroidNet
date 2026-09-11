// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Oxygen.Editor.ContentPipeline.Processes;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Default process runner for bounded content-pipeline tools.
/// </summary>
public sealed class ContentPipelineProcessRunner : IContentPipelineProcessRunner
{
    private readonly Func<ProcessStartInfo, IProgress<ContentPipelineProcessOutput>?, IContentPipelineWorker> startWorker;

    /// <summary>Initializes a new instance of the <see cref="ContentPipelineProcessRunner"/> class.</summary>
    public ContentPipelineProcessRunner()
        : this(WindowsContentPipelineWorker.Start)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="ContentPipelineProcessRunner"/> class with a worker factory.</summary>
    /// <param name="startWorker">The factory transferring ownership of each launched worker.</param>
    internal ContentPipelineProcessRunner(Func<ProcessStartInfo, IContentPipelineWorker> startWorker)
        : this((info, _) => startWorker(info))
    {
    }

    /// <summary>Initializes a new instance of the <see cref="ContentPipelineProcessRunner"/> class with an output-aware worker factory.</summary>
    /// <param name="startWorker">The factory transferring worker ownership and observing its output.</param>
    internal ContentPipelineProcessRunner(Func<ProcessStartInfo, IProgress<ContentPipelineProcessOutput>?, IContentPipelineWorker> startWorker)
    {
        this.startWorker = startWorker;
    }

    /// <inheritdoc />
    [SuppressMessage("Reliability", "CA2025:Do not pass IDisposable instances into unawaited tasks", Justification = "The drain owns the worker until completion and is transferred with termination failures; it does not use the cancellation registration disposed on return.")]
    public async Task<ContentPipelineProcessResult> RunAsync(
        ContentPipelineProcessRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();

        var startInfo = new ProcessStartInfo
        {
            FileName = request.ExecutablePath,
            WorkingDirectory = request.WorkingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };
        foreach (var argument in request.Arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        var worker = this.startWorker(startInfo, request.Output);
        var readerFailure = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var drained = DrainAsync(worker, readerFailure);
        var cancelled = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var registration = cancellationToken.Register(static state => ((TaskCompletionSource)state!).TrySetResult(), cancelled);
        await using var registrationLifetime = registration.ConfigureAwait(false);

        _ = await Task.WhenAny(drained, cancelled.Task, readerFailure.Task).ConfigureAwait(false);
        var terminated = false;
        if (!drained.IsCompleted)
        {
            try
            {
                terminated = worker.Terminate();
            }
            catch (Exception ex)
            {
                // DrainAsync retains the worker and all handles. The caller must
                // retain its inputs/gate too; it must not report Cancelled.
                throw new ContentPipelineTerminationException(ex, drained);
            }
        }

        var result = await drained.ConfigureAwait(false);
        return terminated && cancellationToken.IsCancellationRequested
            ? throw new OperationCanceledException(cancellationToken)
            : result;
    }

    private static async Task<ContentPipelineProcessResult> DrainAsync(
        IContentPipelineWorker worker,
        TaskCompletionSource readerFailure)
    {
        using (worker)
        {
            var output = ObserveReaderAsync(worker.StandardOutput, readerFailure);
            var error = ObserveReaderAsync(worker.StandardError, readerFailure);

            // WhenAll observes BOTH readers even if one fails. None is cancelled:
            // cancellation acts on the owned process tree, then reads reach EOF.
            await Task.WhenAll(worker.Exit, output, error).ConfigureAwait(false);
            return new ContentPipelineProcessResult(
                await worker.Exit.ConfigureAwait(false),
                await output.ConfigureAwait(false),
                await error.ConfigureAwait(false));
        }
    }

    private static async Task<string> ObserveReaderAsync(Task<string> reader, TaskCompletionSource failure)
    {
        try
        {
            return await reader.ConfigureAwait(false);
        }
        catch
        {
            _ = failure.TrySetResult();
            throw;
        }
    }
}
