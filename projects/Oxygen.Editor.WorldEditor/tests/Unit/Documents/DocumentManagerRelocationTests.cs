// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Storage;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Open material and scene documents follow the deletes and moves the editor makes.</summary>
[TestClass]
public sealed class DocumentManagerRelocationTests
{
    private const string Root = "C:/Relocation";

    public TestContext TestContext { get; set; } = null!;

    /// <summary>A deleted material's document closes; a moved one stays open and its tab refreshes.</summary>
    /// <param name="deleted">Whether the material was deleted rather than moved.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task MaterialDocumentClosesOnlyWhenDeleted(bool deleted)
    {
        var window = new WindowId(91);
        var messenger = new StrongReferenceMessenger();
        var projects = new ProjectContextService();
        projects.Activate(new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Relocation",
            Category = Category.Games,
            ProjectRoot = Root,
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
        });
        var documents = new EditorDocumentService();
        var relocation = new Mock<IAssetRelocationService>();
        using var files = new OpenDocuments([], Mock.Of<IAtomicFileStore>());
        using var manager = new DocumentManager(
            documents,
            messenger,
            projects,
            Mock.Of<IProjectUsageService>(),
            Mock.Of<IMaterialDocumentService>(),
            window,
            files,
            Mock.Of<ISceneEngineSync>(),
            relocation.Object);
        var metadata = new MaterialDocumentMetadata(new Uri("asset:///Content/Materials/Red.omat.json")) { Title = "Red" };
        _ = await documents.OpenDocumentAsync(window, metadata).ConfigureAwait(false);
        var source = Path.GetFullPath(Path.Combine(Root, "Content/Materials/Red.omat.json"));
        var target = Path.GetFullPath(Path.Combine(Root, "Content/Materials/Blue.omat.json"));
        var settled = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        documents.DocumentClosed += (_, _) => _ = settled.TrySetResult();
        documents.DocumentMetadataChanged += (_, _) => _ = settled.TrySetResult();

        if (!deleted)
        {
            // The material editor, as a relocation participant, re-pointed its tab before the service reports the move.
            metadata.MaterialUri = new Uri("asset:///Content/Materials/Blue.omat.json");
        }

        relocation.Raise(value => value.FilesChanged += null, deleted
            ? new AssetFilesChangedEventArgs([], [], [source], foldersChanged: false)
            : new AssetFilesChangedEventArgs([new RelocationFileMove(source, target, IsDirectory: false)], [], [], foldersChanged: false));
        await settled.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = documents.GetOpenDocuments(window).OfType<MaterialDocumentMetadata>().Should().HaveCount(deleted ? 0 : 1);
    }

    /// <summary>Deleting a scene's file in the editor closes its tab; the delete already asked about it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SceneDocumentClosesWhenItsFileIsDeleted()
    {
        var window = new WindowId(92);
        using var scenes = new SceneDocumentFiles(Mock.Of<ISceneDocumentCommandService>());
        var (document, _) = await scenes.CreateAsync("Main", this.TestContext.CancellationToken).ConfigureAwait(false);
        var projects = new ProjectContextService();
        projects.Activate(scenes.Context);
        var documents = new EditorDocumentService();
        var relocation = new Mock<IAssetRelocationService>();
        using var manager = new DocumentManager(
            documents,
            new StrongReferenceMessenger(),
            projects,
            Mock.Of<IProjectUsageService>(),
            Mock.Of<IMaterialDocumentService>(),
            window,
            scenes.Documents,
            Mock.Of<ISceneEngineSync>(),
            relocation.Object);
        _ = await documents.OpenDocumentAsync(window, SceneDocumentMetadata.For(document)).ConfigureAwait(false);
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        documents.DocumentClosed += (_, _) => _ = closed.TrySetResult();

        relocation.Raise(value => value.FilesChanged += null, new AssetFilesChangedEventArgs([], [], [Path.GetDirectoryName(document.File.Path)!], foldersChanged: true));
        await closed.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = documents.GetOpenDocuments(window).Should().BeEmpty();
    }
}
