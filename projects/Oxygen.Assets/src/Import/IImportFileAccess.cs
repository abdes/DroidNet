// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Abstracts file I/O for importers.
/// </summary>
/// <remarks>
/// This is intentionally minimal in the MVP scaffolding. Concrete implementations can be layered
/// on top of <c>Oxygen.Storage</c>.
/// </remarks>
public interface IImportFileAccess
{
    /// <summary>
    /// Gets basic metadata for the given project-relative file.
    /// </summary>
    /// <param name="sourcePath">Project-relative path using <c>/</c> separators.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>The file metadata.</returns>
    public ValueTask<ImportFileMetadata> GetMetadataAsync(
        string sourcePath,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Reads a small header prefix from the given project-relative path.
    /// </summary>
    /// <param name="sourcePath">Project-relative path using <c>/</c> separators.</param>
    /// <param name="maxBytes">Maximum number of bytes to read.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>The header bytes (0..maxBytes).</returns>
    public ValueTask<ReadOnlyMemory<byte>> ReadHeaderAsync(
        string sourcePath,
        int maxBytes,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Reads the full contents of the given project-relative file.
    /// </summary>
    /// <param name="sourcePath">Project-relative path using <c>/</c> separators.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>The file contents.</returns>
    public ValueTask<ReadOnlyMemory<byte>> ReadAllBytesAsync(
        string sourcePath,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Writes bytes to the given project-relative output path.
    /// </summary>
    /// <param name="relativePath">Project-relative path using <c>/</c> separators.</param>
    /// <param name="bytes">The bytes to write.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public ValueTask WriteAllBytesAsync(
        string relativePath,
        ReadOnlyMemory<byte> bytes,
        CancellationToken cancellationToken = default);
}
