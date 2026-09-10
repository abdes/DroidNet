// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>
/// Holds prepared documents until closure is approved. Disposing without committing preserves them.
/// </summary>
public sealed partial class DocumentCloseTransaction : IDisposable
{
    private readonly Func<Task<bool>> commit;
    private readonly Action release;
    private bool isCommitted;
    private bool isDisposed;

    /// <summary>Initializes a new instance of the <see cref="DocumentCloseTransaction"/> class.</summary>
    /// <param name="commit">The approved close operation.</param>
    /// <param name="release">The operation that releases preparation state.</param>
    internal DocumentCloseTransaction(Func<Task<bool>> commit, Action release)
    {
        this.commit = commit;
        this.release = release;
    }

    /// <summary>Closes prepared documents exactly once.</summary>
    /// <returns>True when the prepared documents closed; false if they changed after preparation.</returns>
    public async Task<bool> CommitAsync()
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);
        if (this.isCommitted)
        {
            throw new InvalidOperationException("This document close transaction was already committed.");
        }

        this.isCommitted = true;
        return await this.commit().ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (!this.isDisposed)
        {
            this.isDisposed = true;
            this.release();
        }
    }
}
