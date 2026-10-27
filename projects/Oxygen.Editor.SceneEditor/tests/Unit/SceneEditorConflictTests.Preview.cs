// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.LivePreview.Settings;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneEditor.Unit.Tests;

/// <summary>Checks preview preference persistence at the actual document close boundary.</summary>
internal sealed partial class SceneEditorConflictTests
{
    /// <summary>A final control edit accepted during close must finish saving before close preparation completes.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task CloseWaitsForPreviewEditCommittedByTheFocusedControl()
    {
        using var session = new TestSession(TestProject.Create(new MockFileSystem(), Path.GetTempPath()).Context);
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.MaxTargetFps).Returns(1000);
        var store = new Mock<IEditorSettingsManager>();
        var saved = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = store.Setup(value => value.SaveSettingAsync(PreviewSettingsService.Key, It.IsAny<PreviewSettingsService.Preferences>(), It.IsAny<SettingContext>(), It.IsAny<IProgress<SettingsProgress>>(), It.IsAny<CancellationToken>()))
            .Returns(saved.Task);
        using var preferences = new PreviewSettingsService(engine.Object, store.Object, session, Mock.Of<IOperationResultPublisher>(), new OperationStatusReducer());
        await preferences.EnsureRestoredAsync().ConfigureAwait(false);
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true, preferences, () => preferences.FrameRateCap = 45).ConfigureAwait(false);

        var close = fixture.Editor.PrepareForCloseAsync();

        _ = preferences.FrameRateCap.Should().Be(45);
        _ = close.IsCompleted.Should().BeFalse();
        saved.SetResult();
        await close.ConfigureAwait(false);
        _ = close.IsCompletedSuccessfully.Should().BeTrue();
    }
}
