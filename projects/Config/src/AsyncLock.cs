// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1402 // File may only contain a single type

namespace DroidNet.Config;

/// <summary>
///     Lightweight async-compatible mutual exclusion primitive that supports both synchronous and asynchronous
///     disposal.
/// </summary>
/// <remarks>
///     Use <see cref="AcquireAsync(CancellationToken)"/> to obtain a <see cref="Releaser"/> which releases the
///     lock when disposed. The lock is backed by a <see cref="SemaphoreSlim"/> with an initial count of1. The class
///     implements both <see cref="IDisposable"/> and <see cref="IAsyncDisposable"/>; disposal will release the
///     underlying resources and prevent further acquisitions.
/// </remarks>
/// <example>
///     <code><![CDATA[
///     private readonly AsyncLock initializationLock = new();
///
///     public async Task InitializeAsync(CancellationToken ct = default)
///     {
///         var releaser = await this.initializationLock.AcquireAsync(ct).ConfigureAwait(false);
///         await using (releaser.ConfigureAwait(false))
///         {
///             // critical section protected by the AsyncLock
///         }
///     }
///     ]]></code>
/// </example>
public sealed class AsyncLock : IDisposable, IAsyncDisposable
{
    private readonly SemaphoreSlim semaphore = new(1, 1);
    private bool disposed;

    /// <summary>
    ///     Acquires the lock asynchronously.
    /// </summary>
    /// <param name="ct">Token to cancel the acquisition.</param>
    /// <returns>
    ///     A <see cref="ValueTask{T}"/> that completes with a <see cref="Releaser"/> which will release the lock
    ///     when disposed.
    /// </returns>
    public async ValueTask<Releaser> AcquireAsync(CancellationToken ct = default)
    {
        this.ThrowIfDisposed();
        await this.semaphore.WaitAsync(ct).ConfigureAwait(false);
        return new Releaser(this.semaphore);
    }

    /// <summary>
    ///     Releases resources used by the lock.
    /// </summary>
    /// <remarks>
    ///     Calling <see cref="Dispose"/> marks the instance disposed and disposes the underlying
    /// <see cref="SemaphoreSlim"/>.
    /// </remarks>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.semaphore.Dispose();
    }

    /// <summary>
    ///     Asynchronously disposes the lock.
    /// </summary>
    /// <returns>A completed <see cref="ValueTask"/>.</returns>
    /// <see cref="SemaphoreSlim"/> only exposes synchronous disposal; this method forwards to
    /// <see cref="Dispose"/> and returns a completed <see cref="ValueTask"/>.
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0042:Do not use blocking calls in an async method", Justification = "this is DisposeAsync")]
    public ValueTask DisposeAsync()
    {
        this.Dispose(); // SemaphoreSlim has only sync Dispose
        return ValueTask.CompletedTask;
    }

    private void ThrowIfDisposed()
    => ObjectDisposedException.ThrowIf(this.disposed, nameof(AsyncLock));
}

/// <summary>
///     RAII-style releaser returned by <see cref="AsyncLock.AcquireAsync(CancellationToken)"/>.
/// </summary>
/// <param name="toRelease">The <see cref="SemaphoreSlim"/> to release.</param>
/// <remarks>
///     Disposing the releaser releases the underlying <see cref="SemaphoreSlim"/> permit. The struct implements
///     both <see cref="IDisposable"/> and <see cref="IAsyncDisposable"/> so it can be used with both
///     `using` and `await using`.
/// </remarks>
public class Releaser(SemaphoreSlim toRelease) : IDisposable, IAsyncDisposable
{
    private bool disposed;

    /// <summary>
    ///     Releases the held semaphore permit.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    ///     Releases the held semaphore permit asynchronously.
    /// </summary>
    /// <returns>A completed <see cref="ValueTask"/>.</returns>
    public ValueTask DisposeAsync()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
        return ValueTask.CompletedTask;
    }

    /// <summary>
    ///     Releases the held semaphore permit.
    /// </summary>
    /// <param name="disposing">
    ///     <see langword="true"/> to release managed resources; otherwise, <see langword="false"/>.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (!this.disposed)
        {
            if (disposing)
            {
                _ = toRelease?.Release();
            }

            this.disposed = true;
        }
    }
}
