// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.Models.Documents;

/// <summary>
///     The kind of a model document: a model's source file (<c>.glb</c>, <c>.gltf</c>, <c>.fbx</c>), opened to look at what
///     its import gave.
/// </summary>
/// <remarks>
///     The document never writes its source file: the model is an import output, and what the user tunes is its import
///     settings, which it reads from the model's import record and saves there. Its title keeps the suffix, since
///     <c>Car.glb</c> and <c>Car.fbx</c> can sit side by side.
/// </remarks>
/// <param name="session">The project session, whose project holds the models' import records.</param>
/// <param name="writer">Saves a model's import record; none where model documents are only read.</param>
public sealed class ModelDocumentKind(IProjectSession session, IModelImportRecordWriter? writer = null) : IDocumentKind
{
    /// <inheritdoc/>
    public string Name => "Model";

    /// <inheritdoc/>
    public IReadOnlyList<string> FileSuffixes { get; } = [".glb", ".gltf", ".fbx"];

    /// <inheritdoc/>
    public bool TitleKeepsSuffix => true;

    /// <inheritdoc/>
    /// <remarks>The source's bytes are the importer's to read; the document holds which model it shows and its import
    /// record. A model without a readable record opens without import settings.</remarks>
    public Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(file);
        ArgumentNullException.ThrowIfNull(context);
        cancellationToken.ThrowIfCancellationRequested();
        var project = session.Context;
        var records = new ModelImportRecords(project.FileSystem, project.ProjectRoot.FullPath);
        var record = records.TryFindBySource(file.Path.FullPath, out var found) ? ModelImportRecordSnapshot.Read(project.FileSystem, found.RecordPath) : null;
        return Task.FromResult(DocumentDecodeResult.Decoded(new ModelContent(file.Asset, record, context.History, writer)));
    }
}
