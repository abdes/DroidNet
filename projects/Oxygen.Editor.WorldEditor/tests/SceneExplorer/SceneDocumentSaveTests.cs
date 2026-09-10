// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Verifies the scene save outcomes consumed by close preparation.</summary>
[TestClass]
public sealed class SceneDocumentSaveTests
{
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task SaveFailureKeepsSceneDirtyAndPublishesDocumentResult(bool throwsException)
    {
        var projects = new Mock<IProjectManagerService>(MockBehavior.Strict);
        var save = projects.Setup(service => service.SaveSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>()));
        if (throwsException)
        {
            _ = save.ThrowsAsync(new IOException("Disk unavailable"));
        }
        else
        {
            _ = save.ReturnsAsync(value: false);
        }

        var documents = new Mock<IDocumentService>(MockBehavior.Strict);
        var results = new Mock<IOperationResultPublisher>();
        var sut = new SceneDocumentCommandService(
            new Mock<ISceneExplorerService>(MockBehavior.Strict).Object,
            new SceneSelectionService(),
            new Mock<ISceneEngineSync>(MockBehavior.Strict).Object,
            projects.Object,
            documents.Object,
            default,
            new StrongReferenceMessenger(),
            results.Object,
            new OperationStatusReducer());
        var scene = new Scene(Mock.Of<IProject>(project => project.ProjectInfo == Mock.Of<IProjectInfo>(info => info.Location == "H:/SceneSaveTest"))) { Name = "Test Scene" };
        var metadata = new SceneDocumentMetadata(scene.Id) { Title = scene.Name, IsDirty = true };
        var context = new SceneDocumentCommandContext(scene.Id, metadata, scene, new HistoryKeeper(scene));
        context.History.AddChange("Keep edit", () => Task.CompletedTask);

        var result = await sut.SaveSceneAsync(context).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        results.Verify(
            publisher => publisher.Publish(It.Is<OperationResult>(operation =>
                operation.Status == OperationStatus.Failed && operation.AffectedScope.DocumentId == scene.Id)),
            Times.Once);
        documents.Verify(
            service => service.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>()),
            Times.Never);
    }
}
