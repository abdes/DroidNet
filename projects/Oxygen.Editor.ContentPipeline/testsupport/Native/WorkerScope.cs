// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Diagnostics;
using System.Text.Json;
using AwesomeAssertions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class WorkerScope : IAsyncDisposable
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
