// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.Materials.Tests;

/// <summary>Material documents opened from a temporary folder, with the real document kind and edits.</summary>
internal sealed class MaterialDocuments : IDisposable
{
    private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("OxygenMaterials-");

    public MaterialDocuments()
    {
        this.Kind = new MaterialDocumentKind(this.Validation);
        this.Documents = new OpenDocuments([this.Kind], this.Files);
        this.Edits = new MaterialEdits(this.Validation);
    }

    public MaterialValidation Validation { get; } = new();

    public MaterialDocumentKind Kind { get; }

    public IAtomicFileStore Files { get; } = new NativeAtomicFileStore(new RealFileSystem());

    public OpenDocuments Documents { get; }

    public MaterialEdits Edits { get; }

    public static MaterialContent ContentOf(Document document) => (MaterialContent)document.Content;

    public async Task<Document> CreateAsync(string name, CancellationToken cancellationToken)
    {
        // Written as asset authoring writes it: UTF-8 without a byte order mark.
        await File.WriteAllBytesAsync(this.PathOf(name), Encoding.UTF8.GetBytes(AssetTemplates.Material(name)), cancellationToken).ConfigureAwait(false);
        return await this.OpenAsync(name, cancellationToken).ConfigureAwait(false);
    }

    public async Task<Document> OpenAsync(string name, CancellationToken cancellationToken)
    {
        var opened = await this.Documents.OpenAsync(this.FileOf(name), cancellationToken).ConfigureAwait(false);
        return opened.Document ?? throw new InvalidOperationException(opened.Problem);
    }

    public string PathOf(string name) => Path.Combine(this.directory.FullName, name + ".omat.json");

    public DocumentFile FileOf(string name) => new(FileSystemPath.From(this.PathOf(name)), AssetPath.Parse($"/Content/Materials/{name}.omat"));

    public void Dispose()
    {
        this.Documents.Dispose();
        this.directory.Delete(recursive: true);
    }
}
