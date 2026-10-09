// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using AwesomeAssertions;
using Microsoft.EntityFrameworkCore;
using Moq;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Workspace;

/// <summary>
/// Viewport state is user-local workspace state kept per project and scene: it round-trips through
/// the editor settings store, stays isolated per project, never lets a late load replace newer
/// state, and discards a payload it cannot use with a warning.
/// </summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes.")]
public sealed class ViewportStateServiceTests
{
    private static readonly Guid SceneA = Guid.Parse("a1000000-0000-0000-0000-000000000001");
    private static readonly Guid SceneB = Guid.Parse("b2000000-0000-0000-0000-000000000002");

    private static readonly SceneViewportState FourPanes = new(
        SceneViewLayout.FourQuad,
        FocusedPane: 2,
        [
            new(CameraType.Perspective, CameraControlMode.OrbitTrackball, ViewportCameraState.From(new RuntimeEditorCamera(new Vector3(1, 2, 3), Quaternion.CreateFromAxisAngle(Vector3.UnitZ, 0.5f), new Vector3(0, 0, 1), 10)), SceneCameraId: null),
            new(CameraType.Top, CameraControlMode.OrbitTurntable, ViewportCameraState.From(new RuntimeEditorCamera(new Vector3(0, 0, 50), Quaternion.Identity, Vector3.Zero, 25)), SceneCameraId: null),
            new(CameraType.Perspective, CameraControlMode.OrbitTrackball, EditorCamera: null, Guid.Parse("c3000000-0000-0000-0000-000000000003")),
            new(CameraType.Front, CameraControlMode.OrbitTurntable, EditorCamera: null, SceneCameraId: null),
        ]);

    private static readonly SceneViewportState OnePane = new(
        SceneViewLayout.OnePane,
        FocusedPane: 0,
        [new(CameraType.Left, CameraControlMode.OrbitTurntable, EditorCamera: null, SceneCameraId: null)]);

    [TestMethod]
    public async Task StateRoundTripsThroughTheSettingsStore()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        _ = (await service.RestoreAsync(fixture.Project, SceneA).ConfigureAwait(false)).Should().BeNull();

        service.Update(fixture.Project, SceneA, FourPanes);
        service.Update(fixture.Project, SceneB, OnePane);
        await service.FlushAsync().ConfigureAwait(false);

        var reopened = fixture.CreateService();
        var restored = await reopened.RestoreAsync(fixture.Project, SceneA).ConfigureAwait(false);
        _ = restored.Should().NotBeNull();
        _ = restored!.Layout.Should().Be(SceneViewLayout.FourQuad);
        _ = restored.FocusedPane.Should().Be(2);
        _ = restored.Panes.Should().Equal(FourPanes.Panes);
        _ = (await reopened.RestoreAsync(fixture.Project, SceneB).ConfigureAwait(false))!.Panes.Should().Equal(OnePane.Panes);
        _ = fixture.Results.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ProjectsKeepSeparateState()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        service.Update(fixture.Project, SceneA, FourPanes);
        await service.FlushAsync().ConfigureAwait(false);

        var second = Fixture.ProjectAt("Second");
        _ = (await service.RestoreAsync(second, SceneA).ConfigureAwait(false)).Should().BeNull();

        // A new project at a reused path does not inherit the old project's viewports.
        var replacement = Fixture.ProjectAt("First");
        _ = (await fixture.CreateService().RestoreAsync(replacement, SceneA).ConfigureAwait(false)).Should().BeNull();
    }

    [TestMethod]
    public async Task LateLoadNeverReplacesNewerState()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var store = new Mock<IEditorSettingsManager>();
        var loaded = new TaskCompletionSource<ViewportStateService.ProjectViewports?>(TaskCreationOptions.RunContinuationsAsynchronously);
        ViewportStateService.ProjectViewports? written = null;
        _ = store.Setup(value => value.LoadSettingAsync(ViewportStateService.Key, It.IsAny<SettingContext>(), It.IsAny<IProgress<SettingsProgress>>(), It.IsAny<CancellationToken>()))
            .Returns(loaded.Task);
        _ = store.Setup(value => value.SaveSettingAsync(ViewportStateService.Key, It.IsAny<ViewportStateService.ProjectViewports>(), It.IsAny<SettingContext>(), It.IsAny<IProgress<SettingsProgress>>(), It.IsAny<CancellationToken>()))
            .Callback<SettingKey<ViewportStateService.ProjectViewports>, ViewportStateService.ProjectViewports, SettingContext?, IProgress<SettingsProgress>?, CancellationToken>(
                (_, value, _, _, _) => written = value)
            .Returns(Task.CompletedTask);
        var service = fixture.CreateService(store.Object);

        var restoring = service.RestoreAsync(fixture.Project, SceneA);
        service.Update(fixture.Project, SceneA, OnePane);
        loaded.SetResult(new(fixture.Project.ProjectId, ViewportStateService.CurrentVersion, new Dictionary<Guid, SceneViewportState>
        {
            [SceneA] = FourPanes,
            [SceneB] = FourPanes,
        }));

        _ = (await restoring.ConfigureAwait(false)).Should().Be(OnePane, "the update is newer than the stored state");
        await service.FlushAsync().ConfigureAwait(false);
        _ = written!.Scenes.Should().ContainKey(SceneB, "the write carries the stored scenes it did not change");
        _ = written.Scenes[SceneA].Should().Be(OnePane);
    }

    [TestMethod]
    public async Task UnsupportedVersionIsDiscardedWithAWarning()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        await fixture.Store.SaveSettingAsync(
            ViewportStateService.Key,
            new ViewportStateService.ProjectViewports(fixture.Project.ProjectId, ViewportStateService.CurrentVersion + 1, new Dictionary<Guid, SceneViewportState> { [SceneA] = FourPanes }),
            fixture.Context).ConfigureAwait(false);

        var restored = await fixture.CreateService().RestoreAsync(fixture.Project, SceneA).ConfigureAwait(false);

        _ = restored.Should().BeNull();
        _ = fixture.Results.Should().ContainSingle().Which.Diagnostics.Should()
            .ContainSingle(issue => issue.Code == DiagnosticCodes.SettingsPrefix + "VIEWPORT_STATE_UNSUPPORTED");
    }

    [TestMethod]
    public async Task UnreadablePayloadIsDiscardedWithAWarning()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        await fixture.Store.SaveSettingAsync(new SettingKey<string>(ViewportStateService.Key.SettingsModule, ViewportStateService.Key.Name), "not viewport state", fixture.Context)
            .ConfigureAwait(false);
        var service = fixture.CreateService();

        var restored = await service.RestoreAsync(fixture.Project, SceneA).ConfigureAwait(false);
        service.Update(fixture.Project, SceneA, OnePane);
        await service.FlushAsync().ConfigureAwait(false);

        _ = restored.Should().BeNull();
        _ = fixture.Results.Should().ContainSingle().Which.Diagnostics.Should()
            .ContainSingle(issue => issue.Code == DiagnosticCodes.SettingsPrefix + "VIEWPORT_STATE_UNREADABLE");
        var rewritten = await fixture.CreateService().RestoreAsync(fixture.Project, SceneA).ConfigureAwait(false);
        _ = rewritten!.Panes.Should().Equal(OnePane.Panes, "a later write replaces the discarded payload");
    }

    private sealed class Fixture : IAsyncDisposable
    {
        private readonly PersistentState database = new(new DbContextOptionsBuilder<PersistentState>().UseSqlite("Data Source=:memory:").Options);
        private readonly Mock<IOperationResultPublisher> publisher = new();

        public ProjectContext Project { get; } = ProjectAt("First");

        public SettingContext Context => SettingContext.Project(
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(this.Project.ProjectRoot)).ToUpperInvariant());

        public EditorSettingsManager Store => new(this.database);

        public List<OperationResult> Results { get; } = [];

        public static async Task<Fixture> CreateAsync()
        {
            var fixture = new Fixture();
            await fixture.database.Database.OpenConnectionAsync().ConfigureAwait(false);
            _ = await fixture.database.Database.EnsureCreatedAsync().ConfigureAwait(false);
            _ = fixture.publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(fixture.Results.Add);
            return fixture;
        }

        public static ProjectContext ProjectAt(string name) => ProjectContext.FromProjectInfo(new ProjectInfo(
            name, Category.Games, Path.Combine(Path.GetTempPath(), "Oxygen.ViewportState", name), string.Empty)
        {
            AuthoringMounts = [new("Content", "Content")],
        });

        // Each service gets its own store, so nothing is served from another service's cache.
        public ViewportStateService CreateService(IEditorSettingsManager? store = null)
            => new(store ?? this.Store, this.publisher.Object, new OperationStatusReducer());

        public ValueTask DisposeAsync() => this.database.DisposeAsync();
    }
}
