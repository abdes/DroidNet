// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Materials.Tests.TestSupport;

/// <summary>Material documents opened from an in-memory folder, with the real document kind and edits.</summary>
internal sealed partial class MaterialDocuments : IDisposable
{
    private const string Folder = @"C:\Project\Content\Materials";

    public MaterialDocuments()
    {
        _ = this.FileSystem.Directory.CreateDirectory(Folder);
        this.Files = new NativeAtomicFileStore(this.FileSystem);
        this.Kind = new MaterialDocumentKind(this.Validation);
        this.Documents = new OpenDocuments([this.Kind], this.Files);
        this.Edits = new MaterialEdits(this.Validation);
    }

    public MaterialValidation Validation { get; } = new();

    public MaterialDocumentKind Kind { get; }

    public MockFileSystem FileSystem { get; } = new();

    public IAtomicFileStore Files { get; }

    public OpenDocuments Documents { get; }

    public MaterialEdits Edits { get; }

    public static MaterialContent ContentOf(Document document) => (MaterialContent)document.Content;

    public static string PathOf(string name) => Path.Combine(Folder, name + ".omat.json");

    public static DocumentFile FileOf(string name) => new(FileSystemPath.From(PathOf(name)), AssetPath.Parse($"/Content/Materials/{name}.omat"));

    public async Task<Document> CreateAsync(string name, CancellationToken cancellationToken)
    {
        // Written as asset authoring writes it: UTF-8 without a byte order mark.
        await this.FileSystem.File.WriteAllBytesAsync(PathOf(name), Encoding.UTF8.GetBytes(MaterialTemplates.New(name)), cancellationToken).ConfigureAwait(false);
        return await this.OpenAsync(name, cancellationToken).ConfigureAwait(false);
    }

    public async Task<Document> OpenAsync(string name, CancellationToken cancellationToken)
    {
        var opened = await this.Documents.OpenAsync(FileOf(name), cancellationToken).ConfigureAwait(false);
        return opened.Document ?? throw new InvalidOperationException(opened.Problem);
    }

    public void Dispose() => this.Documents.Dispose();
}
