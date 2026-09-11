// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Connects source capture to the document owners' existing save gates.</summary>
public interface ICookDocumentRegistry
{
    /// <summary>Registers a document's source and asynchronous read-lease factory.</summary>
    /// <param name="sourcePath">The canonical saved source path.</param>
    /// <param name="acquire">Acquires the save gate and state; null means the document closed.</param>
    /// <returns>A registration removed when the document closes or changes source identity.</returns>
    public IDisposable Register(string sourcePath, Func<CancellationToken, Task<CookDocumentReadLease?>> acquire);

    /// <summary>Acquires all open documents that contribute to the requested source set.</summary>
    /// <param name="sourcePaths">The files in the resolved dependency closure.</param>
    /// <param name="cancellationToken">Cancels waiting for an owner's save gate.</param>
    /// <returns>The protected document states, including any dirty-document blockers.</returns>
    public Task<CookDocumentReadSet> AcquireAsync(IEnumerable<string> sourcePaths, CancellationToken cancellationToken);
}
