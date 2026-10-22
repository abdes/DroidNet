// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scenes.Tests.Interaction;

/// <summary>Node lock and visibility persistence, reload and scene activation.</summary>
[TestClass]
public sealed partial class SceneNodeInteractionTests
{
    [TestMethod]
    public async Task RestoreAsync_LoadsStoredHiddenAndLock()
    {
        var project = TestProject.Create(new MockFileSystem()).Context;
        var sceneId = SceneId.New();
        var hidden = NodeId.New();
        var locked = NodeId.New();

        var settings = new Mock<IEditorSettingsManager>(MockBehavior.Loose);
        var stored = new SceneNodeInteraction.ProjectInteraction(
            project.ProjectId,
            new Dictionary<SceneId, SceneNodeInteraction.SceneInteraction>
            {
                [sceneId] = new([hidden], [locked], SceneCategories.All),
            });
        _ = settings
            .Setup(s => s.LoadSettingAsync(
                It.IsAny<SettingKey<SceneNodeInteraction.ProjectInteraction>>(),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(stored);

        var service = new SceneNodeInteraction(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(project, sceneId).ConfigureAwait(false);

        _ = service.IsHidden(hidden).Should().BeTrue("a stored hidden id reloads on scene restore");
        _ = service.IsLocked(locked).Should().BeTrue("a stored locked id reloads on scene restore");
    }

    [TestMethod]
    public async Task GetLockOwner_ResolvesNearestAncestorOnlyInActiveScene()
    {
        var project = TestProject.Create(new MockFileSystem()).Context;
        var scene = CreateScene();
        var parent = new SceneNode(scene, "Locked Parent");
        var child = new SceneNode(scene, "Child");
        parent.Hierarchy.AddChild(child);
        scene.RootNodes.Add(parent);
        var otherSceneNode = new SceneNode(CreateScene(), "Other Scene Node");

        var settings = new Mock<IEditorSettingsManager>(MockBehavior.Loose);
        var service = new SceneNodeInteraction(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(project, scene.Id).ConfigureAwait(false);
        service.SetLocked(parent.Id, isLocked: true);

        _ = service.GetLockOwner(child).Should().BeSameAs(parent);
        _ = service.GetLockOwner(parent).Should().BeSameAs(parent);
        _ = service.GetLockOwner(otherSceneNode).Should().BeNull("an inactive scene must not inherit the active scene's lock state");
    }

    [TestMethod]
    public async Task RestoreAsync_IgnoresAnotherProjectsRecord()
    {
        var project = TestProject.Create(new MockFileSystem()).Context;
        var sceneId = SceneId.New();
        var hidden = NodeId.New();

        var settings = new Mock<IEditorSettingsManager>(MockBehavior.Loose);

        // Same path-scope but a different ProjectId: the settings scope is reused by path, so a
        // copied project must not inherit a stranger's hidden set.
        var stored = new SceneNodeInteraction.ProjectInteraction(
            Guid.NewGuid(),
            new Dictionary<SceneId, SceneNodeInteraction.SceneInteraction>
            {
                [sceneId] = new([hidden], [], SceneCategories.All),
            });
        _ = settings
            .Setup(s => s.LoadSettingAsync(
                It.IsAny<SettingKey<SceneNodeInteraction.ProjectInteraction>>(),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(stored);

        var service = new SceneNodeInteraction(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(project, sceneId).ConfigureAwait(false);

        _ = service.IsHidden(hidden).Should().BeFalse("a record whose ProjectId differs is rejected");
    }

    [TestMethod]
    public async Task SceneSwitchAndRestart_PreservesIndependentStates()
    {
        var project = TestProject.Create(new MockFileSystem()).Context;
        var sceneA = SceneId.New();
        var sceneB = SceneId.New();
        var nodeA = NodeId.New();
        var nodeB = NodeId.New();
        SceneNodeInteraction.ProjectInteraction? stored = null;
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(value => value.LoadSettingAsync(
                SceneNodeInteraction.Key,
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Returns(() => Task.FromResult(stored));
        _ = settings.Setup(value => value.SaveSettingAsync(
                SceneNodeInteraction.Key,
                It.IsAny<SceneNodeInteraction.ProjectInteraction>(),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Callback((
                SettingKey<SceneNodeInteraction.ProjectInteraction> _,
                SceneNodeInteraction.ProjectInteraction value,
                SettingContext _,
                IProgress<SettingsProgress> _,
                CancellationToken _) => stored = value)
            .Returns(Task.CompletedTask);
        var service = new SceneNodeInteraction(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(project, sceneA).ConfigureAwait(false);
        service.SetHidden(nodeA, isHidden: true);
        service.SetCategories(new SceneCategories(ShowMeshes: false, ShowLights: true, ShowCameras: false));
        await service.RestoreAsync(project, sceneB).ConfigureAwait(false);
        _ = service.HiddenNodeIds().Should().BeEmpty();
        _ = service.Categories.Should().Be(SceneCategories.All);
        service.SetLocked(nodeB, isLocked: true);
        service.SetCategories(new SceneCategories(ShowMeshes: true, ShowLights: false, ShowCameras: true));
        await service.RestoreAsync(project, sceneA).ConfigureAwait(false);

        _ = service.IsHidden(nodeA).Should().BeTrue();
        _ = service.IsLocked(nodeB).Should().BeFalse();
        _ = service.Categories.Should().Be(new SceneCategories(ShowMeshes: false, ShowLights: true, ShowCameras: false));
        _ = stored!.Scenes.Keys.Should().BeEquivalentTo([sceneA, sceneB]);
        var restarted = new SceneNodeInteraction(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await restarted.RestoreAsync(project, sceneB).ConfigureAwait(false);
        _ = restarted.IsLocked(nodeB).Should().BeTrue();
        _ = restarted.IsHidden(nodeA).Should().BeFalse();
        _ = restarted.Categories.Should().Be(new SceneCategories(ShowMeshes: true, ShowLights: false, ShowCameras: true));
    }

    [TestMethod]
    public async Task InvalidSavedState_PublishesFailureAndKeepsDefaults()
    {
        var project = TestProject.Create(new MockFileSystem()).Context;
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(value => value.LoadSettingAsync(
                SceneNodeInteraction.Key,
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SceneNodeInteraction.ProjectInteraction(project.ProjectId, null!));
        var results = new CapturingOperationResultPublisher();
        var service = new SceneNodeInteraction(settings.Object, results, new OperationStatusReducer());

        await service.RestoreAsync(project, SceneId.New()).ConfigureAwait(false);

        _ = service.HiddenNodeIds().Should().BeEmpty();
        _ = service.Categories.Should().Be(SceneCategories.All);
        _ = results.Published.SelectMany(result => result.Diagnostics).Should()
            .Contain(diagnostic => diagnostic.Code == DiagnosticCodes.SettingsPrefix + "INVALID_STATE");
    }

    [TestMethod]
    public async Task StaleRestoreAfterClear_DoesNotReapplyPriorState()
    {
        var project = TestProject.Create(new MockFileSystem()).Context;
        var sceneId = SceneId.New();
        var hidden = NodeId.New();
        var load = new TaskCompletionSource<SceneNodeInteraction.ProjectInteraction?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(value => value.LoadSettingAsync(
                SceneNodeInteraction.Key,
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Returns(load.Task);
        var service = new SceneNodeInteraction(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        var restoration = service.RestoreAsync(project, sceneId);
        service.ClearActiveScene();
        load.SetResult(new SceneNodeInteraction.ProjectInteraction(
            project.ProjectId,
            new Dictionary<SceneId, SceneNodeInteraction.SceneInteraction>
            {
                [sceneId] = new([hidden], [], new SceneCategories(ShowMeshes: false, ShowLights: false, ShowCameras: false)),
            }));

        await restoration.ConfigureAwait(false);

        _ = service.ActiveSceneId.Should().Be(SceneId.Empty);
        _ = service.IsHidden(hidden).Should().BeFalse();
        _ = service.Categories.Should().Be(SceneCategories.All);
    }

    [TestMethod]
    public async Task ClearActiveScene_ResetsCategoriesWithoutSavingEmptyScene()
    {
        var settings = new Mock<IEditorSettingsManager>();
        var service = new SceneNodeInteraction(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(TestProject.Create(new MockFileSystem()).Context, SceneId.New()).ConfigureAwait(false);
        service.SetCategories(new SceneCategories(ShowMeshes: false, ShowLights: false, ShowCameras: false));

        service.ClearActiveScene();

        _ = service.ActiveSceneId.Should().Be(SceneId.Empty);
        _ = service.Categories.Should().Be(SceneCategories.All);
        _ = service.HiddenNodeIds().Should().BeEmpty();
    }

    private static Scene CreateScene() => new() { Name = "Test Scene" };

    private sealed class CapturingOperationResultPublisher : IOperationResultPublisher
    {
        public List<OperationResult> Published { get; } = [];

        public void Publish(OperationResult result) => this.Published.Add(result);

        public IDisposable Subscribe(IObserver<OperationResult> observer) => new NoopDisposable();
    }

    private sealed partial class NoopDisposable : IDisposable
    {
        public void Dispose()
        {
        }
    }
}
