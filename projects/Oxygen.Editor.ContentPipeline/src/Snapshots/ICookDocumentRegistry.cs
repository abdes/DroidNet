// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Connects source capture to the document owners' existing save gates.</summary>
public interface ICookDocumentRegistry
{
    /// <summary>Occurs when an owner publishes state or closes; callbacks may arrive on its authoring thread.</summary>
    public event EventHandler<CookDocumentStateChangedEventArgs>? StateChanged;

    /// <summary>Reads current presentation facts without entering document save gates.</summary>
    /// <returns>A versioned snapshot of registered owner state.</returns>
    public CookDocumentRegistrySnapshot GetState();

    /// <summary>Registers a document's source and asynchronous read-lease factory.</summary>
    /// <param name="sourcePath">The canonical saved source path.</param>
    /// <param name="acquire">Acquires the save gate and state; null means the document closed.</param>
    /// <returns>A registration removed when the document closes or changes source identity.</returns>
    public ICookDocumentRegistration Register(string sourcePath, Func<CancellationToken, Task<CookDocumentReadLease?>> acquire);

    /// <summary>Acquires all open documents that contribute to the requested source set.</summary>
    /// <param name="sourcePaths">The files in the resolved dependency closure.</param>
    /// <param name="cancellationToken">Cancels waiting for an owner's save gate.</param>
    /// <returns>The protected document states, including any dirty-document blockers.</returns>
    public Task<CookDocumentReadSet> AcquireAsync(IEnumerable<string> sourcePaths, CancellationToken cancellationToken);
}
