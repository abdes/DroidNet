// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Protects the exact output bytes inspected and validated by the native adapter.</summary>
internal sealed partial class CookOutputReadLease : IAsyncDisposable, IDisposable
{
    private readonly string root;
    private readonly Dictionary<string, FileStream> files = [with(StringComparer.Ordinal)];
    private readonly Lock verificationGate = new();
    private Task<CookedInventoryReport>? inventory;
    private Task? nativeDrain;
    private Task? closing;
    private bool cleanupTransferred;
    private volatile bool disposed;

    private CookOutputReadLease(string root) => this.root = root;

    /// <summary>Gets a value indicating whether the adapter produced a physical native index.</summary>
    public bool HasIndex => this.files.ContainsKey("container.index.bin");

    /// <summary>Opens all existing output files without permitting replacement during validation.</summary>
    /// <param name="root">The physical cooked root.</param>
    /// <param name="cancellationToken">Cancels acquisition.</param>
    /// <param name="requireIndex">Whether an absent index should yield an empty lease; explicit reports also inspect incomplete roots.</param>
    /// <returns>The acquired lease; an adapter without physical output yields an empty lease.</returns>
    public static async Task<CookOutputReadLease> AcquireAsync(string root, CancellationToken cancellationToken, bool requireIndex = true)
    {
        if (!Directory.Exists(root) || (requireIndex && !File.Exists(Path.Combine(root, "container.index.bin"))))
        {
            return new(root);
        }

        var lease = new CookOutputReadLease(root);
        try
        {
            foreach (var path in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
            {
                cancellationToken.ThrowIfCancellationRequested();
                lease.files.Add(Path.GetRelativePath(root, path).Replace('\\', '/'), new(path, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan));
            }

            lease.CheckMembership();
            return lease;
        }
        catch
        {
            await lease.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    /// <summary>Verifies this protected opening once through the native inventory authority.</summary>
    /// <param name="native">The existing native content API.</param>
    /// <param name="cancellationToken">Cancels native work.</param>
    /// <param name="artifacts">Optional artifacts already retained by the operation.</param>
    /// <returns>The lease-scoped inventory and any damaged members.</returns>
    public Task<CookedInventoryReport> ReadInventoryAsync(IEngineContentPipelineApi native, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
    {
        lock (this.verificationGate)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            this.CheckMembership();
            return this.inventory ??= this.VerifyInventoryAsync(native, artifacts, cancellationToken);
        }
    }

    private async Task<CookedInventoryReport> VerifyInventoryAsync(IEngineContentPipelineApi native, NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        CookedInventoryReport report;
        try
        {
            report = await native.ReadInventoryAsync(this.root, artifacts, cancellationToken).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException failure)
        {
            Task cleanup;
            lock (this.verificationGate)
            {
                this.nativeDrain = failure.DrainCompletion;
                this.disposed = true;
                this.cleanupTransferred = true;
                cleanup = this.closing ??= this.CloseAfterInspectionAsync(verification: null);
            }

            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, cleanup);
        }

        ObjectDisposedException.ThrowIf(this.disposed, this);
        if (!this.files.TryGetValue("container.index.bin", out var index))
        {
            throw new InvalidDataException("The protected cooked root has no index.");
        }

        index.Position = 0;
        var digest = Convert.ToHexString(await SHA256.HashDataAsync(index, cancellationToken).ConfigureAwait(false));
        if (index.Length != report.IndexSize || !string.Equals(digest, report.IndexSha256, StringComparison.OrdinalIgnoreCase))
        {
            throw new IOException("The native inventory does not describe this protected index.");
        }

        this.CheckMembership();
        return report;
    }

    /// <summary>Returns the actual protected file set, including files not represented by index file records.</summary>
    /// <returns>Root-relative paths and sizes without reading file contents.</returns>
    public IReadOnlyList<CookedFileEntry> GetFiles()
    {
        this.CheckMembership();
        return this.files.OrderBy(static pair => pair.Key, StringComparer.Ordinal)
            .Select(static pair => new CookedFileEntry(pair.Key, checked((ulong)pair.Value.Length))).ToArray();
    }

    /// <inheritdoc />
    public void Dispose() => _ = this.BeginDisposal();

    /// <inheritdoc />
    public ValueTask DisposeAsync() => new(this.BeginDisposal());

    private Task BeginDisposal()
    {
        lock (this.verificationGate)
        {
            this.disposed = true;
            if (this.cleanupTransferred)
            {
                return Task.CompletedTask;
            }

            return this.closing ??= this.CloseAfterInspectionAsync(this.inventory);
        }
    }

    private async Task CloseAfterInspectionAsync(Task<CookedInventoryReport>? verification)
    {
        if (verification is not null)
        {
            // Observe completion without replacing the operation's original failure.
            await verification.ContinueWith(static completed => { _ = completed.Exception; }, CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default).ConfigureAwait(false);
        }

        try
        {
            if (this.nativeDrain is not null)
            {
                await this.nativeDrain.ConfigureAwait(false);
            }
        }
        finally
        {
            foreach (var stream in this.files.Values)
            {
                await stream.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private void CheckMembership()
    {
        var current = Directory.EnumerateFiles(this.root, "*", SearchOption.AllDirectories).Select(path => Path.GetRelativePath(this.root, path).Replace('\\', '/')).ToHashSet(StringComparer.Ordinal);
        if (!current.SetEquals(this.files.Keys))
        {
            throw new IOException("Cooked output changed while its files were being validated.");
        }
    }
}
