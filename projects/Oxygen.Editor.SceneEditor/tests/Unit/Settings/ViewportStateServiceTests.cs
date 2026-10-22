// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Microsoft.EntityFrameworkCore;
using Moq;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneEditor.Unit.Tests.Settings;

/// <summary>
/// Viewport state is user-local workspace state kept per project and scene: it round-trips through
/// the editor settings store, stays isolated per project session, survives a mount change, never
/// lets a late load replace newer state, and discards a payload it cannot use with a warning.
/// </summary>
[TestClass]
internal sealed class ViewportStateServiceTests
{
    private static readonly SceneId SceneA = SceneId.FromValue(Guid.Parse("a1000000-0000-0000-0000-000000000001"));
    private static readonly SceneId SceneB = SceneId.FromValue(Guid.Parse("b2000000-0000-0000-0000-000000000002"));

    private static readonly SceneViewportState FourPanes = new(
        SceneViewLayout.FourQuad,
        FocusedPane: 2,
        [
            new(CameraType.Perspective, CameraControlMode.OrbitTrackball, ViewportCameraState.From(new RuntimeEditorCamera(new Vector3(1, 2, 3), Quaternion.CreateFromAxisAngle(Vector3.UnitZ, 0.5f), new Vector3(0, 0, 1), 10)), SceneCameraId: null),
            new(CameraType.Top, CameraControlMode.OrbitTurntable, ViewportCameraState.From(new RuntimeEditorCamera(new Vector3(0, 0, 50), Quaternion.Identity, Vector3.Zero, 25)), SceneCameraId: null),
            new(CameraType.Perspective, CameraControlMode.OrbitTrackball, EditorCamera: null, NodeId.FromValue(Guid.Parse("c3000000-0000-0000-0000-000000000003"))),
            new(CameraType.Front, CameraControlMode.OrbitTurntable, EditorCamera: null, SceneCameraId: null),
        ]);

    private static readonly SceneViewportState OnePane = new(
        SceneViewLayout.OnePane,
        FocusedPane: 0,
        [new(CameraType.Left, CameraControlMode.OrbitTurntable, EditorCamera: null, SceneCameraId: null)]);

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task StateRoundTripsThroughTheSettingsStore()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        _ = (await service.RestoreAsync(SceneA).ConfigureAwait(false)).Should().BeNull();

        service.Update(SceneA, FourPanes);
        service.Update(SceneB, OnePane);
        await service.FlushAsync().ConfigureAwait(false);

        var reopened = fixture.CreateService();
        var restored = await reopened.RestoreAsync(SceneA).ConfigureAwait(false);
        _ = restored.Should().NotBeNull();
        _ = restored!.Layout.Should().Be(SceneViewLayout.FourQuad);
        _ = restored.FocusedPane.Should().Be(2);
        _ = restored.Panes.Should().Equal(FourPanes.Panes);
        _ = (await reopened.RestoreAsync(SceneB).ConfigureAwait(false))!.Panes.Should().Equal(OnePane.Panes);
        _ = fixture.Results.Should().BeEmpty();
    }

    [TestMethod]
    public async Task SessionsKeepTheirProjectsState()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var first = fixture.CreateService();
        first.Update(SceneA, FourPanes);
        await first.FlushAsync().ConfigureAwait(false);

        var second = fixture.CreateService(session: fixture.OpenSession(Fixture.ProjectAt("Second")));
        _ = (await second.RestoreAsync(SceneA).ConfigureAwait(false)).Should().BeNull("another project's session has its own state");
        second.Update(SceneA, OnePane);
        await second.FlushAsync().ConfigureAwait(false);

        var reopened = await fixture.CreateService().RestoreAsync(SceneA).ConfigureAwait(false);
        _ = reopened!.Panes.Should().Equal(FourPanes.Panes, "the other session's write does not reach this project");

        // A new project at a reused path does not inherit the old project's viewports.
        var replacement = fixture.CreateService(session: fixture.OpenSession(Fixture.ProjectAt("First")));
        _ = (await replacement.RestoreAsync(SceneA).ConfigureAwait(false)).Should().BeNull();
    }

    [TestMethod]
    public async Task MountChangeKeepsTheProjectsState()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        _ = await service.RestoreAsync(SceneA).ConfigureAwait(false);

        // A mount change replaces the context with the same project id and root (scope topology, decision 7).
        fixture.Session.ChangeContext(fixture.Project with { CookedContentOrder = [] });
        service.Update(SceneA, OnePane);
        await service.FlushAsync().ConfigureAwait(false);

        var restored = await fixture.CreateService(session: fixture.OpenSession(fixture.Project)).RestoreAsync(SceneA).ConfigureAwait(false);
        _ = restored!.Panes.Should().Equal(OnePane.Panes, "the project's settings scope is its root, which a mount change keeps");
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

        var restoring = service.RestoreAsync(SceneA);
        service.Update(SceneA, OnePane);
        loaded.SetResult(new(fixture.Project.ProjectId, ViewportStateService.CurrentVersion, new Dictionary<SceneId, SceneViewportState>
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
            new ViewportStateService.ProjectViewports(fixture.Project.ProjectId, ViewportStateService.CurrentVersion + 1, new Dictionary<SceneId, SceneViewportState> { [SceneA] = FourPanes }),
            fixture.Context,
            ct: this.TestContext.CancellationToken).ConfigureAwait(false);

        var restored = await fixture.CreateService().RestoreAsync(SceneA).ConfigureAwait(false);

        _ = restored.Should().BeNull();
        _ = fixture.Results.Should().ContainSingle().Which.Diagnostics.Should()
            .ContainSingle(issue => issue.Code == DiagnosticCodes.SettingsPrefix + "VIEWPORT_STATE_UNSUPPORTED");
    }

    [TestMethod]
    public async Task UnreadablePayloadIsDiscardedWithAWarning()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        await fixture.Store.SaveSettingAsync(new SettingKey<string>(ViewportStateService.Key.SettingsModule, ViewportStateService.Key.Name), "not viewport state", fixture.Context, ct: this.TestContext.CancellationToken)
            .ConfigureAwait(false);
        var service = fixture.CreateService();

        var restored = await service.RestoreAsync(SceneA).ConfigureAwait(false);
        service.Update(SceneA, OnePane);
        await service.FlushAsync().ConfigureAwait(false);

        _ = restored.Should().BeNull();
        _ = fixture.Results.Should().ContainSingle().Which.Diagnostics.Should()
            .ContainSingle(issue => issue.Code == DiagnosticCodes.SettingsPrefix + "VIEWPORT_STATE_UNREADABLE");
        var rewritten = await fixture.CreateService().RestoreAsync(SceneA).ConfigureAwait(false);
        _ = rewritten!.Panes.Should().Equal(OnePane.Panes, "a later write replaces the discarded payload");
    }

    private sealed class Fixture : IAsyncDisposable
    {
        private readonly PersistentState database = new(new DbContextOptionsBuilder<PersistentState>().UseSqlite("Data Source=:memory:").Options);
        private readonly Mock<IOperationResultPublisher> publisher = new();
        private readonly List<TestSession> sessions = [];

        private Fixture()
        {
            this.Project = ProjectAt("First");
            this.Session = this.OpenSession(this.Project);
        }

        public ProjectContext Project { get; }

        public TestSession Session { get; }

        public SettingContext Context => SettingContext.Project(
            Path.TrimEndingDirectorySeparator(this.Project.ProjectRoot.FullPath).ToUpperInvariant());

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

        public static ProjectContext ProjectAt(string name)
            => TestProject.Create(new MockFileSystem(), Path.Combine(Path.GetTempPath(), "Oxygen.ViewportState", name)).Context with { Name = name };

        public TestSession OpenSession(ProjectContext project)
        {
            var session = new TestSession(project);
            this.sessions.Add(session);
            return session;
        }

        // Each service gets its own store, so nothing is served from another service's cache.
        public ViewportStateService CreateService(IEditorSettingsManager? store = null, TestSession? session = null)
            => new(session ?? this.Session, store ?? this.Store, this.publisher.Object, new OperationStatusReducer());

        public ValueTask DisposeAsync()
        {
            foreach (var session in this.sessions)
            {
                session.Dispose();
            }

            return this.database.DisposeAsync();
        }
    }
}
