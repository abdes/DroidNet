// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Models.Documents;

/// <summary>
///     The kind of a model document: a model's source file (<c>.glb</c>, <c>.gltf</c>, <c>.fbx</c>), opened to look at what
///     its import gave.
/// </summary>
/// <remarks>
///     The document never writes its source file: the model is an import output, and what the user tunes is its import
///     settings. Its title keeps the suffix, since <c>Car.glb</c> and <c>Car.fbx</c> can sit side by side.
/// </remarks>
public sealed class ModelDocumentKind : IDocumentKind
{
    /// <inheritdoc/>
    public string Name => "Model";

    /// <inheritdoc/>
    public IReadOnlyList<string> FileSuffixes { get; } = [".glb", ".gltf", ".fbx"];

    /// <inheritdoc/>
    public bool TitleKeepsSuffix => true;

    /// <inheritdoc/>
    /// <remarks>The source's bytes are the importer's to read; the document holds which model it shows.</remarks>
    public Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(file);
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(DocumentDecodeResult.Decoded(new ModelContent(file.Asset)));
    }
}
