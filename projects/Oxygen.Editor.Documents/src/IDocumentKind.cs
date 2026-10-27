// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>A kind of document: the files it opens and how it turns their bytes into the model it edits.</summary>
/// <remarks>
/// A kind holds no file version and no history, and never reads or writes a file: <see cref="Document"/> does that the
/// same way for every kind. The module that owns the model registers the kind.
/// </remarks>
public interface IDocumentKind
{
    /// <summary>Gets the kind's name, as people read it, such as "Material".</summary>
    public string Name { get; }

    /// <summary>Gets the file suffixes the kind opens, each starting with a dot, such as ".omat.json".</summary>
    /// <remarks>No two kinds open the same suffix; the longest suffix that ends a file's name chooses its kind.</remarks>
    public IReadOnlyList<string> FileSuffixes { get; }

    /// <summary>Gets a value indicating whether a document's title keeps its file's suffix, such as <c>Car.glb</c>.</summary>
    /// <remarks>
    /// A kind whose files differ only by their suffix in one folder, such as <c>Car.glb</c> and <c>Car.fbx</c>, keeps it;
    /// by default the title drops it.
    /// </remarks>
    public bool TitleKeepsSuffix => false;

    /// <summary>Turns a file's bytes into the content a document edits.</summary>
    /// <param name="file">The file the bytes were read from.</param>
    /// <param name="content">The file's complete bytes.</param>
    /// <param name="context">The identity and the history of the document the content is for.</param>
    /// <param name="cancellationToken">Cancels decoding.</param>
    /// <returns>The content, or why the bytes are not a document of this kind.</returns>
    public Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken);
}
