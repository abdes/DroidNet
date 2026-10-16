// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Documents;
using Testably.Abstractions;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Material documents in a temporary folder, opened with the material kind, as the editor opens them.</summary>
internal sealed class MaterialDocuments : IDisposable
{
    private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("OxygenMaterialDocuments-");

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

    public string Folder => this.directory.FullName;

    public static MaterialContent ContentOf(Document document) => (MaterialContent)document.Content;

    /// <summary>Writes a new material, as New Material does, and opens it.</summary>
    public async Task<Document> CreateAsync(string name, CancellationToken cancellationToken)
    {
        var path = this.PathOf(name);
        // Written as asset authoring writes it: UTF-8 without a byte order mark.
        await File.WriteAllBytesAsync(path, Encoding.UTF8.GetBytes(AssetTemplates.Material(name)), cancellationToken).ConfigureAwait(false);
        return await this.OpenAsync(name, cancellationToken).ConfigureAwait(false);
    }

    public async Task<Document> OpenAsync(string name, CancellationToken cancellationToken)
    {
        var opened = await this.Documents.OpenAsync(this.FileOf(name), cancellationToken).ConfigureAwait(false);
        return opened.Document ?? throw new InvalidOperationException(opened.Problem);
    }

    public string PathOf(string name) => Path.Combine(this.Folder, name + ".omat.json");

    public DocumentFile FileOf(string name) => new(this.PathOf(name), new Uri($"asset:///Content/Materials/{name}.omat.json"));

    public DocumentTab TabOf(Document document) => new(document, new DocumentEditorDescriptor(this.Kind, static _ => new object()));

    /// <summary>Creates the material editor of a document, with a project that holds the temporary folder.</summary>
    public MaterialEditorViewModel EditorOf(Document document, IAssetFeed? feed = null, IDocumentInputCommitter? inputCommitter = null)
        => new(
            this.TabOf(document),
            this.Edits,
            feed ?? new FakeAssetFeed(),
            System.Reactive.Concurrency.ImmediateScheduler.Instance,
            Mock.Of<IMaterialCookService>(),
            Mock.Of<IProjectContextService>(),
            inputCommitter: inputCommitter);

    public void Dispose()
    {
        this.Documents.Dispose();
        this.directory.Delete(recursive: true);
    }
}
