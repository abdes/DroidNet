// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Models;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ModelEditor.TestSupport;

/// <summary>
/// The record writer test documents save models with, made before the services whose imports write: it forwards to its
/// <see cref="Target"/>, which <see cref="TestModelEditorServices"/> sets.
/// </summary>
internal sealed class LateRecordWriter : IModelImportRecordWriter
{
    /// <summary>Gets or sets the writer the saves go to.</summary>
    public IModelImportRecordWriter? Target { get; set; }

    /// <summary>Writes the import record of a test model, as an import would, before the model opens.</summary>
    /// <param name="documents">The test documents, whose project holds the model.</param>
    /// <param name="source">The model's source.</param>
    /// <param name="settings">The model's import settings, as JSON.</param>
    /// <param name="change">Changes the record before it is written, such as with what an import established.</param>
    /// <returns>The record.</returns>
    public static ModelImportSettings WriteRecord(TestSceneDocuments documents, AssetPath source, string settings = """{"name":"Car"}""", Func<ModelImportSettings, ModelImportSettings>? change = null)
    {
        ArgumentNullException.ThrowIfNull(documents);
        ArgumentNullException.ThrowIfNull(source);
        var files = documents.Project.Context.FileSystem;
        var path = ModelImportSettings.PathFor(documents.Project.Root.FullPath, Guid.CreateVersion7());
        using var parsed = JsonDocument.Parse(settings);
        var record = ModelImportSettings.Create(path, documents.Project.PathOf(source.VirtualPath).FullPath, source.VirtualPath, parsed.RootElement);
        record = change?.Invoke(record) ?? record;
        _ = files.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        files.File.WriteAllBytes(path, record.ToBytes());
        return record;
    }

    /// <inheritdoc/>
    public Task<DocumentSaveResult> ReplaceAsync(AssetPath model, ModelImportSettings expected, ModelImportSettings replacement, CancellationToken cancellationToken)
        => (this.Target ?? throw new InvalidOperationException("No writer took the saves yet.")).ReplaceAsync(model, expected, replacement, cancellationToken);
}
