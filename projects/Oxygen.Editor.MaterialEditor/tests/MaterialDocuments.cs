// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Material documents in an in-memory folder, opened with the material kind, as the editor opens them.</summary>
internal sealed partial class MaterialDocuments : IDisposable
{
    private readonly List<DocumentTab> tabs = [];

    public MaterialDocuments()
    {
        _ = this.FileSystem.Directory.CreateDirectory(Folder);
        this.Files = new NativeAtomicFileStore(this.FileSystem);
        this.Kind = new MaterialDocumentKind(this.Validation);
        this.Documents = new OpenDocuments([this.Kind], this.Files);
        this.Edits = new MaterialEdits(this.Validation);
    }

    public static string Folder => @"C:\Project\Content\Materials";

    public MaterialValidation Validation { get; } = new();

    public MaterialDocumentKind Kind { get; }

    public MockFileSystem FileSystem { get; } = new();

    public IAtomicFileStore Files { get; }

    public OpenDocuments Documents { get; }

    public MaterialEdits Edits { get; }

    public static MaterialContent ContentOf(Document document) => (MaterialContent)document.Content;

    public static string PathOf(string name) => Path.Combine(Folder, name + ".omat.json");

    public static DocumentFile FileOf(string name) => new(FileSystemPath.From(PathOf(name)), AssetPath.Parse($"/Content/Materials/{name}.omat"));

    /// <summary>Writes a new material, as New Material does, and opens it.</summary>
    /// <param name="name">The material's name, which is its file's name.</param>
    /// <param name="cancellationToken">Cancels the write and the open.</param>
    /// <returns>The material's document.</returns>
    public async Task<Document> CreateAsync(string name, CancellationToken cancellationToken)
    {
        var path = PathOf(name);

        // Written as asset authoring writes it: UTF-8 without a byte order mark.
        await this.FileSystem.File.WriteAllBytesAsync(path, Encoding.UTF8.GetBytes(MaterialTemplates.New(name)), cancellationToken).ConfigureAwait(false);
        return await this.OpenAsync(name, cancellationToken).ConfigureAwait(false);
    }

    public async Task<Document> OpenAsync(string name, CancellationToken cancellationToken)
    {
        var opened = await this.Documents.OpenAsync(FileOf(name), cancellationToken).ConfigureAwait(false);
        return opened.Document ?? throw new InvalidOperationException(opened.Problem);
    }

    /// <summary>Places a document in a tab, which the fixture disposes, as the document area owns its tabs.</summary>
    /// <param name="document">The document.</param>
    /// <returns>The tab.</returns>
    public DocumentTab TabOf(Document document)
    {
        var tab = new DocumentTab(document, new DocumentEditorDescriptor(this.Kind, static _ => new object()));
        this.tabs.Add(tab);
        return tab;
    }

    /// <summary>Creates the material editor of a document, over the in-memory file system.</summary>
    /// <param name="document">The document the editor shows, placed in a tab of its own.</param>
    /// <param name="feed">The asset feed the editor's pickers read, or an empty one.</param>
    /// <param name="inputCommitter">Commits the focused control's input before a save, if any.</param>
    /// <returns>The editor.</returns>
    public MaterialEditorViewModel EditorOf(Document document, IAssetFeed? feed = null, IDocumentInputCommitter? inputCommitter = null)
        => new(
            this.TabOf(document),
            this.Edits,
            feed ?? new FakeAssetFeed(),
            System.Reactive.Concurrency.ImmediateScheduler.Instance,
            Mock.Of<IMaterialCookService>(),
            Mock.Of<IProjectContextService>(),
            this.FileSystem,
            inputCommitter: inputCommitter);

    public void Dispose()
    {
        foreach (var tab in this.tabs)
        {
            tab.Dispose();
        }

        this.Documents.Dispose();
    }
}
