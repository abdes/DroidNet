// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Open material documents follow Content Browser deletes and moves.</summary>
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
            Scenes = [],
        });
        var documents = new EditorDocumentService();
        using var manager = new DocumentManager(
            documents,
            messenger,
            projects,
            Mock.Of<IProjectUsageService>(),
            Mock.Of<IMaterialDocumentService>(),
            window,
            Mock.Of<IProjectManagerService>(),
            Mock.Of<ISceneEngineSync>());
        var metadata = new MaterialDocumentMetadata(new Uri("asset:///Content/Materials/Red.omat.json")) { Title = "Red" };
        _ = await documents.OpenDocumentAsync(window, metadata).ConfigureAwait(false);
        var source = Path.GetFullPath(Path.Combine(Root, "Content/Materials/Red.omat.json"));
        var target = Path.GetFullPath(Path.Combine(Root, "Content/Materials/Blue.omat.json"));
        var settled = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        documents.DocumentClosed += (_, _) => _ = settled.TrySetResult();
        documents.DocumentMetadataChanged += (_, _) => _ = settled.TrySetResult();

        if (!deleted)
        {
            // The material editor, as a relocation participant, re-pointed its tab before the browser announces the move.
            metadata.MaterialUri = new Uri("asset:///Content/Materials/Blue.omat.json");
        }

        _ = messenger.Send(deleted
            ? new AssetFilesChangedMessage([], [], [source])
            : new AssetFilesChangedMessage([new RelocationFileMove(source, target, IsDirectory: false)], [], []));
        await settled.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = documents.GetOpenDocuments(window).OfType<MaterialDocumentMetadata>().Should().HaveCount(deleted ? 0 : 1);
    }
}
