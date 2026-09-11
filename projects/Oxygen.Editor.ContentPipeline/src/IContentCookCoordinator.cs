// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Owns the single editor cook writer and its project lifetime.</summary>
public interface IContentCookCoordinator
{
    /// <summary>Queues work and enters its saved-input/publication scope only after acquiring the writer.</summary>
    /// <typeparam name="T">The workflow result.</typeparam>
    /// <param name="work">Work receiving the captured project and its linked cancellation token.</param>
    /// <param name="cancellationToken">Cancels the queued or active request.</param>
    /// <returns>The work's result after successful completion in the same project lifetime.</returns>
    public Task<T> RunAsync<T>(Func<ContentCookOperation, CancellationToken, Task<T>> work, CancellationToken cancellationToken);

    /// <summary>Rejects callbacks or publication belonging to a replaced or closed project.</summary>
    /// <param name="operation">The operation whose lifetime must still be current.</param>
    public void VerifyCurrent(ContentCookOperation operation);

    /// <summary>Rejects input capture or publication outside the operation's active writer scope.</summary>
    /// <param name="operation">The operation that must still own the writer.</param>
    public void VerifyWriter(ContentCookOperation operation);
}
