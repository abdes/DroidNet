// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Checks preview preference persistence at the actual document close boundary.</summary>
public sealed partial class SceneEditorConflictTests
{
    /// <summary>A final control edit accepted during close must finish saving before close preparation completes.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task CloseWaitsForPreviewEditCommittedByTheFocusedControl()
    {
        var projects = new ProjectContextService();
        var project = ProjectContext.FromProjectInfo(new ProjectInfo("Preview close", Category.Games, Path.GetTempPath(), string.Empty));
        projects.Activate(project);
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.MaxTargetFps).Returns(1000);
        var store = new Mock<IEditorSettingsManager>();
        var saved = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = store.Setup(value => value.SaveSettingAsync(PreviewSettingsService.Key, It.IsAny<PreviewSettingsService.Preferences>(), It.IsAny<SettingContext>(), It.IsAny<IProgress<SettingsProgress>>(), It.IsAny<CancellationToken>()))
            .Returns(saved.Task);
        var preferences = new PreviewSettingsService(engine.Object, store.Object, projects, Mock.Of<IOperationResultPublisher>(), new OperationStatusReducer());
        await preferences.RestoreAsync(project).ConfigureAwait(false);
        using var fixture = new Fixture(active: true, preferences, () => preferences.RunAtFps = 45);

        var close = fixture.Editor.PrepareForCloseAsync();

        _ = preferences.RunAtFps.Should().Be(45);
        _ = close.IsCompleted.Should().BeFalse();
        saved.SetResult();
        await close.ConfigureAwait(false);
        _ = close.IsCompletedSuccessfully.Should().BeTrue();
    }
}
