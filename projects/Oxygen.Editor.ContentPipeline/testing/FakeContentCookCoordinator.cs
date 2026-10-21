// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Testing;

/// <summary>
/// An <see cref="IContentCookCoordinator"/> for tests: one writer at a time over the session's project, without the
/// cross-process cook leases <see cref="ContentCookCoordinator"/> takes on disk, so it serves a project on any file
/// system.
/// </summary>
/// <remarks>
/// Each request runs on its own, in the order it arrives; pending cooks are not coalesced. An operation is current
/// while its project is the session's project and the session's lifetime goes on, and owns the writer while its work
/// runs.
/// </remarks>
/// <param name="session">The project session.</param>
public sealed class FakeContentCookCoordinator(IProjectSession session) : IContentCookCoordinator, IDisposable
{
    private readonly SemaphoreSlim writer = new(1, 1);
    private readonly Lock gate = new();
    private ContentCookOperation? active;

    /// <inheritdoc/>
    public Task<T> RunCookAsync<T>(CookRunRequest request, Func<ContentCookOperation, CancellationToken, Task<T>> work, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        return this.RunCoreAsync(work, verifyCurrentAfterWork: true, cancellationToken);
    }

    /// <inheritdoc/>
    public Task<T> RunAsync<T>(Func<ContentCookOperation, CancellationToken, Task<T>> work, CancellationToken cancellationToken)
        => this.RunCoreAsync(work, verifyCurrentAfterWork: true, cancellationToken);

    /// <inheritdoc/>
    public Task RunProjectChangeAsync(Func<ContentCookOperation, CancellationToken, Task> change, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(change);
        return this.RunCoreAsync(
            async (operation, token) =>
            {
                await change(operation, token).ConfigureAwait(false);
                return true;
            },
            verifyCurrentAfterWork: false,
            cancellationToken);
    }

    /// <inheritdoc/>
    public void VerifyCurrent(ContentCookOperation operation)
    {
        ArgumentNullException.ThrowIfNull(operation);
        if (session.Lifetime.IsCancellationRequested || !ReferenceEquals(operation.Project, session.Context))
        {
            throw new OperationCanceledException("The cook request's project was closed or replaced.", new CancellationToken(canceled: true));
        }
    }

    /// <inheritdoc/>
    public void VerifyWriter(ContentCookOperation operation)
    {
        this.VerifyCurrent(operation);
        lock (this.gate)
        {
            if (!ReferenceEquals(operation, this.active))
            {
                throw new InvalidOperationException("The cook operation no longer owns the writer.");
            }
        }
    }

    /// <inheritdoc/>
    public void Dispose() => this.writer.Dispose();

    private async Task<T> RunCoreAsync<T>(Func<ContentCookOperation, CancellationToken, Task<T>> work, bool verifyCurrentAfterWork, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(work);
        await this.writer.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            session.Lifetime.ThrowIfCancellationRequested();
            var operation = new ContentCookOperation(Guid.NewGuid(), session.Context, ProjectLifetime: 0);
            lock (this.gate)
            {
                this.active = operation;
            }

            try
            {
                var result = await work(operation, cancellationToken).ConfigureAwait(false);
                if (verifyCurrentAfterWork)
                {
                    this.VerifyCurrent(operation);
                }

                return result;
            }
            finally
            {
                lock (this.gate)
                {
                    this.active = null;
                }
            }
        }
        finally
        {
            _ = this.writer.Release();
        }
    }
}
