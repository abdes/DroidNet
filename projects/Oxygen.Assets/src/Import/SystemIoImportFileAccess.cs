// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// System.IO-based implementation of <see cref="IImportFileAccess"/>.
/// </summary>
public sealed class SystemIoImportFileAccess : IImportFileAccess
{
    private readonly string projectRoot;

    /// <summary>
    /// Initializes a new instance of the <see cref="SystemIoImportFileAccess"/> class.
    /// </summary>
    /// <param name="projectRoot">Absolute project root path.</param>
    public SystemIoImportFileAccess(string projectRoot)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        this.projectRoot = Path.GetFullPath(projectRoot);
    }

    /// <inheritdoc />
    public ValueTask<ImportFileMetadata> GetMetadataAsync(
        string sourcePath,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);

        var fullPath = this.ResolveFullPath(sourcePath);
        if (!File.Exists(fullPath))
        {
            throw new FileNotFoundException("Missing file.", fullPath);
        }

        var info = new FileInfo(fullPath);
        var lastWriteTimeUtc = DateTime.SpecifyKind(info.LastWriteTimeUtc, DateTimeKind.Utc);
        return ValueTask.FromResult(new ImportFileMetadata(
                ByteLength: info.Length,
                LastWriteTimeUtc: new DateTimeOffset(lastWriteTimeUtc)));
    }

    /// <inheritdoc />
    public async ValueTask<ReadOnlyMemory<byte>> ReadHeaderAsync(
        string sourcePath,
        int maxBytes,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);
        ArgumentOutOfRangeException.ThrowIfNegative(maxBytes);

        var fullPath = this.ResolveFullPath(sourcePath);
        if (!File.Exists(fullPath))
        {
            throw new FileNotFoundException("Missing file.", fullPath);
        }

        var stream = new FileStream(
            fullPath,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            bufferSize: 16 * 1024,
            options: FileOptions.Asynchronous | FileOptions.SequentialScan);

        try
        {
            var len = checked((int)Math.Min(maxBytes, stream.Length));
            if (len == 0)
            {
                return ReadOnlyMemory<byte>.Empty;
            }

            var buffer = new byte[len];
            var read = 0;
            while (read < len)
            {
                var n = await stream.ReadAsync(buffer.AsMemory(read, len - read), cancellationToken).ConfigureAwait(false);
                if (n == 0)
                {
                    break;
                }

                read += n;
            }

            return buffer.AsMemory(0, read);
        }
        finally
        {
            await stream.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code clarity")]
    public async ValueTask<ReadOnlyMemory<byte>> ReadAllBytesAsync(
        string sourcePath,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);
        var fullPath = this.ResolveFullPath(sourcePath);

        if (!File.Exists(fullPath))
        {
            throw new FileNotFoundException("Missing file.", fullPath);
        }

        return await File.ReadAllBytesAsync(fullPath, cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async ValueTask WriteAllBytesAsync(
        string relativePath,
        ReadOnlyMemory<byte> bytes,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(relativePath);

        var fullPath = this.ResolveFullPath(relativePath);
        var dir = Path.GetDirectoryName(fullPath);
        if (!string.IsNullOrWhiteSpace(dir))
        {
            Directory.CreateDirectory(dir);
        }

        await File.WriteAllBytesAsync(fullPath, bytes.ToArray(), cancellationToken).ConfigureAwait(false);
    }

    private string ResolveFullPath(string projectRelative)
    {
        // Import paths use '/' separators. Convert to platform separator.
        var safe = projectRelative.Replace('/', Path.DirectorySeparatorChar);
        return Path.GetFullPath(Path.Combine(this.projectRoot, safe));
    }
}
