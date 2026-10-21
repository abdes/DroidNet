// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Security.Cryptography;
using System.Text;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Materials;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>The material editor over an in-memory material, for the tests of its views.</summary>
internal static partial class MaterialEditorModels
{
    /// <summary>Opens an in-memory material in the material editor.</summary>
    /// <param name="assetProvider">The asset feed the editor reads, or one that never changes.</param>
    /// <returns>The editor, with the open documents and tab it shows, all disposed together.</returns>
    internal static MaterialEditorModel CreateMaterialEditor(IAssetFeed? assetProvider = null)
    {
        // The file is held in memory, so the material opens at once.
        var validation = new MaterialValidation();
        var kind = new MaterialDocumentKind(validation);
        var files = new MemoryFiles(Encoding.UTF8.GetBytes(MaterialTemplates.New("UI")));
        var documents = new OpenDocuments([kind], files);
        try
        {
            var opening = documents.OpenAsync(new DocumentFile(FileSystemPath.From("C:/UI/Content/Materials/UI.omat.json"), AssetPath.Parse("/Content/Materials/UI.omat")));
            if (!opening.IsCompletedSuccessfully || opening.Result.Document is not { } document)
            {
                throw new InvalidOperationException("An in-memory material opens at once.");
            }

            return new MaterialEditorModel(documents, new DocumentTab(document, new DocumentEditorDescriptor(kind, static _ => new object())), validation, assetProvider);
        }
        catch
        {
            documents.Dispose();
            throw;
        }
    }

    /// <summary>A material editor over an in-memory material, owning the open documents and the tab it shows.</summary>
    /// <param name="documents">The open documents that hold the material.</param>
    /// <param name="tab">The material's tab.</param>
    /// <param name="validation">The material rules the edits check.</param>
    /// <param name="assetProvider">The asset feed the editor reads, or one that never changes.</param>
    internal sealed partial class MaterialEditorModel(OpenDocuments documents, DocumentTab tab, MaterialValidation validation, IAssetFeed? assetProvider) : IDisposable
    {
        /// <summary>Gets the material editor.</summary>
        public MaterialEditorViewModel Editor { get; } = new(
            tab,
            new MaterialEdits(validation),
            assetProvider ?? Mock.Of<IAssetFeed>(feed => feed.Items == Observable.Never<IReadOnlyList<AssetEntry>>()),
            CreateStatusHosting().DispatcherScheduler,
            Mock.Of<IMaterialCookService>(),
            Mock.Of<IProjectContextService>());

        /// <inheritdoc/>
        public void Dispose()
        {
            this.Editor.Dispose();
            tab.Dispose();
            documents.Dispose();
        }
    }

    /// <summary>Holds one file's bytes in memory, whatever its path.</summary>
    private sealed class MemoryFiles(byte[] initial) : IAtomicFileStore
    {
        private byte[] content = initial;

        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
            => Task.FromResult(new FileSnapshot([.. this.content], VersionOf(this.content)));

        public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> written, FileVersion expected, CancellationToken cancellationToken = default)
        {
            this.content = written.ToArray();
            return Task.FromResult(VersionOf(this.content));
        }

        private static FileVersion VersionOf(byte[] bytes) => new(Exists: true, Convert.ToHexString(SHA256.HashData(bytes)));
    }
}
