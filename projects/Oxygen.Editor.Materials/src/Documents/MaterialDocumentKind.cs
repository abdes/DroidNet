// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.Materials.Documents;

/// <summary>
///     The material document kind: a material's source descriptor, edited as a <see cref="MaterialContent"/>.
/// </summary>
/// <param name="validation">Checks sources against the material schemas before they are saved.</param>
public sealed class MaterialDocumentKind(MaterialValidation validation) : IDocumentKind
{
    /// <inheritdoc/>
    public string Name => "Material";

    /// <inheritdoc/>
    public IReadOnlyList<string> FileSuffixes { get; } = [AssetFileTypes.MaterialDescriptor];

    /// <inheritdoc/>
    /// <remarks>The material is named after its file, as its title is.</remarks>
    public Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(file);
        cancellationToken.ThrowIfCancellationRequested();
        MaterialSource source;
        try
        {
            source = MaterialSourceReader.Read(content.Span);
        }
        catch (AssetFormatException exception)
        {
            return Task.FromResult(DocumentDecodeResult.Failed($"'{file.Path.Name}' is not a valid material: {exception.Message}"));
        }

        var named = source with { Name = DocumentKinds.TitleOf(this, file.Path) };
        return Task.FromResult(DocumentDecodeResult.Decoded(new MaterialContent(this, named, validation)));
    }
}
