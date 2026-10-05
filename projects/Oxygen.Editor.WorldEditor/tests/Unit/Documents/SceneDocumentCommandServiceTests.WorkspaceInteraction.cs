// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO;
using System.Linq;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Workspace;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Workspace interaction persistence reload and the pending-suppression warning cadence.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task WorkspaceInteractionService_RestoreAsync_LoadsStoredHiddenAndLock()
    {
        var project = ProjectContext.FromProjectInfo(SlotTestProjectInfo);
        var sceneId = Guid.NewGuid();
        var hidden = Guid.NewGuid();
        var locked = Guid.NewGuid();

        var settings = new Mock<IEditorSettingsManager>(MockBehavior.Loose);
        var stored = new WorkspaceInteractionService.ProjectInteraction(
            project.ProjectId,
            new Dictionary<Guid, WorkspaceInteractionService.SceneInteraction>
            {
                [sceneId] = new([hidden], [locked], SceneCategories.All),
            });
        _ = settings
            .Setup(s => s.LoadSettingAsync(
                It.IsAny<SettingKey<WorkspaceInteractionService.ProjectInteraction>>(),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(stored);

        var service = new WorkspaceInteractionService(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(project, sceneId).ConfigureAwait(false);

        _ = service.IsHidden(hidden).Should().BeTrue("a stored hidden id reloads on scene restore");
        _ = service.IsLocked(locked).Should().BeTrue("a stored locked id reloads on scene restore");
    }

    [TestMethod]
    public async Task WorkspaceInteractionService_RestoreAsync_IgnoresAnotherProjectsRecord()
    {
        var project = ProjectContext.FromProjectInfo(SlotTestProjectInfo);
        var sceneId = Guid.NewGuid();
        var hidden = Guid.NewGuid();

        var settings = new Mock<IEditorSettingsManager>(MockBehavior.Loose);
        // Same path-scope but a different ProjectId: the settings scope is reused by path, so a
        // copied project must not inherit a stranger's hidden set.
        var stored = new WorkspaceInteractionService.ProjectInteraction(
            Guid.NewGuid(),
            new Dictionary<Guid, WorkspaceInteractionService.SceneInteraction>
            {
                [sceneId] = new([hidden], [], SceneCategories.All),
            });
        _ = settings
            .Setup(s => s.LoadSettingAsync(
                It.IsAny<SettingKey<WorkspaceInteractionService.ProjectInteraction>>(),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(stored);

        var service = new WorkspaceInteractionService(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(project, sceneId).ConfigureAwait(false);

        _ = service.IsHidden(hidden).Should().BeFalse("a record whose ProjectId differs is rejected");
    }

    [TestMethod]
    public async Task WorkspaceInteractionService_SceneSwitchAndRestart_PreservesIndependentStates()
    {
        var project = ProjectContext.FromProjectInfo(SlotTestProjectInfo);
        var sceneA = Guid.NewGuid();
        var sceneB = Guid.NewGuid();
        var nodeA = Guid.NewGuid();
        var nodeB = Guid.NewGuid();
        WorkspaceInteractionService.ProjectInteraction? stored = null;
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(value => value.LoadSettingAsync(
                WorkspaceInteractionService.Key,
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Returns(() => Task.FromResult(stored));
        _ = settings.Setup(value => value.SaveSettingAsync(
                WorkspaceInteractionService.Key,
                It.IsAny<WorkspaceInteractionService.ProjectInteraction>(),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Callback((
                SettingKey<WorkspaceInteractionService.ProjectInteraction> _,
                WorkspaceInteractionService.ProjectInteraction value,
                SettingContext _,
                IProgress<SettingsProgress> _,
                CancellationToken _) => stored = value)
            .Returns(Task.CompletedTask);
        var service = new WorkspaceInteractionService(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(project, sceneA).ConfigureAwait(false);
        service.SetHidden(nodeA, isHidden: true);
        service.SetCategories(new SceneCategories(false, true, false));
        await service.RestoreAsync(project, sceneB).ConfigureAwait(false);
        _ = service.HiddenNodeIds().Should().BeEmpty();
        _ = service.Categories.Should().Be(SceneCategories.All);
        service.SetLocked(nodeB, isLocked: true);
        service.SetCategories(new SceneCategories(true, false, true));
        await service.RestoreAsync(project, sceneA).ConfigureAwait(false);

        _ = service.IsHidden(nodeA).Should().BeTrue();
        _ = service.IsLocked(nodeB).Should().BeFalse();
        _ = service.Categories.Should().Be(new SceneCategories(false, true, false));
        _ = stored!.Scenes.Keys.Should().BeEquivalentTo([sceneA, sceneB]);
        var restarted = new WorkspaceInteractionService(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await restarted.RestoreAsync(project, sceneB).ConfigureAwait(false);
        _ = restarted.IsLocked(nodeB).Should().BeTrue();
        _ = restarted.IsHidden(nodeA).Should().BeFalse();
        _ = restarted.Categories.Should().Be(new SceneCategories(true, false, true));
    }

    [TestMethod]
    public async Task WorkspaceInteractionService_InvalidSavedState_PublishesFailureAndKeepsDefaults()
    {
        var project = ProjectContext.FromProjectInfo(SlotTestProjectInfo);
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(value => value.LoadSettingAsync(
                WorkspaceInteractionService.Key,
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(new WorkspaceInteractionService.ProjectInteraction(project.ProjectId, null!));
        var results = new CapturingOperationResultPublisher();
        var service = new WorkspaceInteractionService(settings.Object, results, new OperationStatusReducer());

        await service.RestoreAsync(project, Guid.NewGuid()).ConfigureAwait(false);

        _ = service.HiddenNodeIds().Should().BeEmpty();
        _ = service.Categories.Should().Be(SceneCategories.All);
        _ = results.Published.SelectMany(result => result.Diagnostics).Should()
            .Contain(diagnostic => diagnostic.Code == DiagnosticCodes.SettingsPrefix + "INVALID_STATE");
    }

    [TestMethod]
    public async Task WorkspaceInteractionService_StaleRestoreAfterClear_DoesNotReapplyPriorState()
    {
        var project = ProjectContext.FromProjectInfo(SlotTestProjectInfo);
        var sceneId = Guid.NewGuid();
        var hidden = Guid.NewGuid();
        var load = new TaskCompletionSource<WorkspaceInteractionService.ProjectInteraction?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(value => value.LoadSettingAsync(
                WorkspaceInteractionService.Key,
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Returns(load.Task);
        var service = new WorkspaceInteractionService(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        var restoration = service.RestoreAsync(project, sceneId);
        service.ClearActiveScene();
        load.SetResult(new WorkspaceInteractionService.ProjectInteraction(
            project.ProjectId,
            new Dictionary<Guid, WorkspaceInteractionService.SceneInteraction>
            {
                [sceneId] = new([hidden], [], new SceneCategories(false, false, false)),
            }));

        await restoration.ConfigureAwait(false);

        _ = service.ActiveSceneId.Should().Be(Guid.Empty);
        _ = service.IsHidden(hidden).Should().BeFalse();
        _ = service.Categories.Should().Be(SceneCategories.All);
    }

    [TestMethod]
    public async Task WorkspaceInteractionService_ClearActiveScene_ResetsCategoriesWithoutSavingEmptyScene()
    {
        var settings = new Mock<IEditorSettingsManager>();
        var service = new WorkspaceInteractionService(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(ProjectContext.FromProjectInfo(SlotTestProjectInfo), Guid.NewGuid()).ConfigureAwait(false);
        service.SetCategories(new SceneCategories(false, false, false));

        service.ClearActiveScene();

        _ = service.ActiveSceneId.Should().Be(Guid.Empty);
        _ = service.Categories.Should().Be(SceneCategories.All);
        _ = service.HiddenNodeIds().Should().BeEmpty();
    }

    [TestMethod]
    public async Task EditingViewMaskService_WarnsOncePerSceneActivation()
    {
        var project = ProjectContext.FromProjectInfo(SlotTestProjectInfo);
        var settings = new Mock<IEditorSettingsManager>(MockBehavior.Loose);
        var interaction = new WorkspaceInteractionService(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        var results = new CapturingOperationResultPublisher();

        using var mask = new EditingViewMaskService(interaction, results, new OperationStatusReducer()).Start();

        var sceneA = Guid.NewGuid();
        await interaction.RestoreAsync(project, sceneA).ConfigureAwait(false);

        // First change on the active scene reports the pending native suppression once (Q2), even
        // though several hidden entries follow within the same activation.
        interaction.SetHidden(Guid.NewGuid(), isHidden: true);
        interaction.SetHidden(Guid.NewGuid(), isHidden: true);
        _ = MaskWarnings(results).Should().Be(1, "one mask per scene activation (Q3)");

        // A different scene activation is allowed to warn again.
        var sceneB = Guid.NewGuid();
        await interaction.RestoreAsync(project, sceneB).ConfigureAwait(false);
        interaction.SetHidden(Guid.NewGuid(), isHidden: true);
        _ = MaskWarnings(results).Should().Be(2, "a new activation re-reports");
    }

    private static int MaskWarnings(CapturingOperationResultPublisher results)
        => results.Published
            .SelectMany(p => p.Diagnostics)
            .Count(d => d.Code == DiagnosticCodes.SettingsPrefix + "EDITING_VIEW_MASK_UNAVAILABLE");
}
