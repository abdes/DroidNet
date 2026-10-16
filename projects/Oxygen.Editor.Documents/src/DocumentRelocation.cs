// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;

namespace Oxygen.Editor.Documents;

/// <summary>What a relocation did to an open document's file.</summary>
/// <param name="File">The document's file after the relocation; the same file when only its references moved.</param>
/// <param name="Version">The file's version after the relocation, whether it rewrote the file or not.</param>
/// <param name="MapReference">
/// Maps an authored reference, a full asset URI or a virtual path in any of the forms a document writes, to its new
/// value in the same form; it returns <see langword="null"/> when the referenced asset did not move. A folder that moved
/// moves every reference inside it.
/// </param>
public sealed record DocumentRelocation(DocumentFile File, FileVersion Version, Func<string, string?> MapReference)
{
    /// <summary>Maps an asset URI to its new URI.</summary>
    /// <param name="reference">The asset URI.</param>
    /// <returns>The new URI, or <see langword="null"/> when the asset did not move.</returns>
    public Uri? MapUri(Uri reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        return this.MapReference(reference.ToString()) is { } mapped ? new Uri(mapped) : null;
    }
}
