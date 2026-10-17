// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>A file store whose reads can wait for a test to let them through.</summary>
/// <param name="inner">The store that reads and writes the files.</param>
internal sealed class GatedFileStore(IAtomicFileStore inner) : IAtomicFileStore
{
    /// <summary>Gets or sets what a read waits for, given the file's path.</summary>
    public Func<string, Task>? BeforeRead { get; set; }

    /// <inheritdoc/>
    public async Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
    {
        if (this.BeforeRead is { } gate)
        {
            await gate(path).ConfigureAwait(false);
        }

        return await inner.ReadAsync(path, cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
        => inner.WriteAsync(path, content, expected, cancellationToken);
}
