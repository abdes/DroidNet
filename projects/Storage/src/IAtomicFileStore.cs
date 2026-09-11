// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Storage;

/// <summary>Reads coherent content baselines and replaces complete files under a destination write lease.</summary>
public interface IAtomicFileStore
{
    /// <summary>Reads an owned snapshot, including an explicit missing-file version.</summary>
    /// <param name="path">The destination path.</param>
    /// <param name="cancellationToken">Cancels reading.</param>
    /// <returns>The complete content and its version.</returns>
    public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default);

    /// <summary>Writes a complete snapshot only if the destination still matches the expected version.</summary>
    /// <param name="path">The destination path.</param>
    /// <param name="content">The bytes to persist; copied before the first asynchronous operation.</param>
    /// <param name="expected">The opened or last-saved baseline; Missing means create without overwriting.</param>
    /// <param name="cancellationToken">Cancels before replacement; cancellation after replacement does not undo success.</param>
    /// <returns>The newly persisted content identity.</returns>
    /// <exception cref="StorageWriteConflictException">The file changed or another writer owns the destination.</exception>
    /// <remarks>
    /// Writes and flushes a same-directory temporary file before replacement. This protects
    /// the last successful save from a process interruption before commit; it does not promise
    /// universal hardware power-loss durability. External tools must not race the final replacement.
    /// </remarks>
    public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default);
}
