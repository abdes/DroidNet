// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Retains output ownership through native refresh, suspension and teardown.</summary>
public sealed partial class EngineService
{
    private readonly List<IDisposable> cookedContentReaders = [];

    /// <inheritdoc />
    public async Task RefreshProjectCookedRootsAsync(IReadOnlyList<string> paths, IDisposable? readLease = null, bool keepPaused = false)
    {
        var unclaimed = readLease;
        var entered = false;
        try
        {
            await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
            entered = true;
            var runtime = this.EnsureIsRunning();
            var previousReaders = this.cookedContentReaders.ToArray();
            if (readLease is not null)
            {
                this.cookedContentReaders.Add(readLease);
                unclaimed = null;
            }

            await this.AwaitRuntimeOperationAsync(runtime.Commands.ReplaceCookedRootsAsync(paths)).ConfigureAwait(true);
            foreach (var reader in previousReaders)
            {
                reader.Dispose();
                _ = this.cookedContentReaders.Remove(reader);
            }

            if (!keepPaused)
            {
                await this.AwaitRuntimeOperationAsync(runtime.Commands.SetCookedContentPausedAsync(paused: false)).ConfigureAwait(true);
            }
        }
        finally
        {
            try
            {
                unclaimed?.Dispose();
            }
            finally
            {
                if (entered)
                {
                    _ = this.lifecycleGate.Release();
                }
            }
        }
    }

    /// <inheritdoc />
    public async Task SuspendCookedContentAsync()
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runtime = this.EnsureIsRunning();
            await this.AwaitRuntimeOperationAsync(runtime.Commands.SetCookedContentPausedAsync(paused: true)).ConfigureAwait(true);
            this.ReleaseCookedContentReaders();
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc />
    public async Task ResumeCookedContentAsync()
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runtime = this.EnsureIsRunning();
            await this.AwaitRuntimeOperationAsync(runtime.Commands.SetCookedContentPausedAsync(paused: false)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    private async Task AwaitRuntimeOperationAsync(Task operation)
    {
        var loop = this.engineLoopTask;
        if (loop is not null && await Task.WhenAny(operation, loop).ConfigureAwait(true) == loop && !operation.IsCompleted)
        {
            _ = this.ObserveAbandonedContentOperationAsync(operation);
            throw new InvalidOperationException("The engine stopped before acknowledging the content operation.");
        }

        await operation.ConfigureAwait(true);
    }

    private void ReleaseCookedContentReaders()
    {
        List<Exception> failures = [];
        foreach (var reader in this.cookedContentReaders.ToArray())
        {
            if (this.TryCleanup(reader.Dispose, "Release cooked content reader", failures))
            {
                _ = this.cookedContentReaders.Remove(reader);
            }
        }

        ThrowCleanupFailures(failures);
    }

    private async Task ObserveAbandonedContentOperationAsync(Task operation)
    {
        List<Exception> failures = [];
        _ = await this.TryCleanupAsync(() => operation, "Complete interrupted content operation", failures).ConfigureAwait(false);
    }
}
