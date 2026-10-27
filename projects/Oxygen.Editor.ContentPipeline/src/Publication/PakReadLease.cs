// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>
///     Holds a pak file open for shared reading, so no writer replaces it while a cook or runtime uses the metadata read
///     through the same handle.
/// </summary>
internal sealed partial class PakReadLease : IDisposable
{
    private readonly FileStream stream;

    private PakReadLease(FileStream stream, PakDocument document)
    {
        this.stream = stream;
        this.Document = document;
    }

    /// <summary>Gets the pak's browsable metadata, read through the held handle.</summary>
    public PakDocument Document { get; }

    /// <summary>Opens a pak for shared reading and reads its metadata.</summary>
    /// <param name="path">The pak file.</param>
    /// <param name="cancellationToken">Cancels before the metadata is read.</param>
    /// <returns>The lease, which the caller disposes.</returns>
    /// <exception cref="IOException">The pak is missing, unreadable or being written.</exception>
    /// <exception cref="InvalidDataException">The pak is malformed or uses another format version.</exception>
    public static async Task<PakReadLease> AcquireAsync(FileSystemPath path, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(path);
        var opened = new FileStream(path.FullPath, FileMode.Open, FileAccess.Read, FileShare.Read, 4096, FileOptions.Asynchronous);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            return new(opened, PakIndex.Read(opened));
        }
        catch
        {
            await opened.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    /// <inheritdoc />
    public void Dispose() => this.stream.Dispose();
}
