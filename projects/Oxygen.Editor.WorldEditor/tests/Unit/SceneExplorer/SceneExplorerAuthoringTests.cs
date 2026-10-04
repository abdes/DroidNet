// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Routing;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures create and delete authoring go through the document command owner.</summary>
[TestClass]
public sealed class SceneExplorerAuthoringTests
{
    [TestMethod]
    public async Task AddEntity_RoutesThroughCreateNodeCommand()
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        _ = harness.Commands
            .Setup(value => value.CreateNodeAsync(It.IsAny<SceneDocumentCommandContext>(), null, null, It.IsAny<string>()))
            .ReturnsAsync(SceneCommandResults.Success(node));
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        await explorer.AddEntityCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        harness.Commands.Verify(
            value => value.CreateNodeAsync(It.IsAny<SceneDocumentCommandContext>(), null, null, It.IsAny<string>()),
            Times.Once);
    }

    [TestMethod]
    public async Task CreateFolder_RoutesThroughCreateFolderCommand()
    {
        var harness = new AuthoringHarness(out var scene, out _);
        _ = harness.Commands
            .Setup(value => value.CreateFolderAsync(It.IsAny<SceneDocumentCommandContext>(), null, null, "New Folder"))
            .ReturnsAsync(SceneCommandResults.Success(Guid.NewGuid()));
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        await explorer.CreateFolderCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        harness.Commands.Verify(
            value => value.CreateFolderAsync(It.IsAny<SceneDocumentCommandContext>(), null, null, "New Folder"),
            Times.Once);
    }

    private sealed class AuthoringHarness
    {
        private readonly Scene scene;

        public AuthoringHarness(out Scene scene, out SceneNode node)
        {
            var project = new Project(new ProjectInfo("Authoring", Category.Games, "H:/AuthoringTreeTests", "preview.png")) { Name = "Authoring" };
            scene = new Scene(project) { Name = "Scene" };
            node = new SceneNode(scene) { Name = "Node" };
            scene.RootNodes.Add(node);
            project.Scenes.Add(scene);
            this.scene = scene;

            var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
            _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
            this.Manager = manager;

            var documents = new Mock<IDocumentService>();
            var metadata = new SceneDocumentMetadata(scene.Id);
            _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([metadata]);
            _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(metadata.DocumentId);
            this.Documents = documents;

            var sync = new Mock<ISceneEngineSync>();
            _ = sync.Setup(value => value.GetDocumentScene(metadata)).Returns(scene);
            _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata)).Returns(value: true);
            _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
            this.Sync = sync;

            this.Commands = new Mock<ISceneDocumentCommandService>(MockBehavior.Strict);
        }

        public Mock<IProjectManagerService> Manager { get; }

        public Mock<IDocumentService> Documents { get; }

        public Mock<ISceneEngineSync> Sync { get; }

        public Mock<ISceneDocumentCommandService> Commands { get; }

        public SceneExplorerViewModel Build()
            => new(
                this.Manager.Object,
                new StrongReferenceMessenger(),
                Mock.Of<IRouter>(),
                this.Documents.Object,
                default,
                this.Sync.Object,
                new SceneSelectionService(),
                this.Commands.Object);
    }
}
