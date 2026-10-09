// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.EntityFrameworkCore;
using Moq;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Workspace;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Workspace;

/// <summary>Checks project preference persistence independently of the native rendering loop.</summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes.")]
public sealed class PreviewSettingsTests
{
    /// <summary>
    /// The defaults impose no frame rate: vsync on, no cap, idle panes left idle. Accepted edits reach the engine
    /// and survive a fresh service and settings cache.
    /// </summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task PreferencesRoundTripThroughSqlite()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        _ = fixture.Engine.Object.TargetFps.Should().Be(0);
        _ = fixture.Engine.Object.EngineLoggingVerbosity.Should().Be(-2);
        fixture.Engine.Verify(value => value.SetVSyncEnabled(true), Times.Once());
        fixture.Engine.Verify(value => value.SetAlwaysRenderPanes(false), Times.Once());
        _ = service.MaxFrameRateCap.Should().Be(1000);

        service.FrameRateCapEnabled = true;
        service.FrameRateCap = 45;
        service.VSyncEnabled = false;
        service.AlwaysRenderPanes = true;
        service.LoggingVerbosity = -1;
        await service.FlushAsync().ConfigureAwait(false);
        _ = fixture.Engine.Object.TargetFps.Should().Be(45);
        fixture.Engine.Verify(value => value.SetVSyncEnabled(false), Times.Once());
        fixture.Engine.Verify(value => value.SetAlwaysRenderPanes(true), Times.Once());

        var reopened = fixture.CreateService();
        await reopened.RestoreAsync(fixture.Project).ConfigureAwait(false);
        _ = reopened.FrameRateCapEnabled.Should().BeTrue();
        _ = reopened.FrameRateCap.Should().Be(45);
        _ = reopened.VSyncEnabled.Should().BeFalse();
        _ = reopened.AlwaysRenderPanes.Should().BeTrue();
        _ = reopened.LoggingVerbosity.Should().Be(-1);
        _ = fixture.Results.Should().BeEmpty();
    }

    /// <summary>Turning the cap off uncaps the engine but keeps the chosen cap for later.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task DisablingTheCapUncapsTheEngineAndKeepsTheValue()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        service.FrameRateCapEnabled = true;
        service.FrameRateCap = 90;

        service.FrameRateCapEnabled = false;

        _ = fixture.Engine.Object.TargetFps.Should().Be(0);
        _ = service.FrameRateCap.Should().Be(90);
        service.FrameRateCapEnabled = true;
        _ = fixture.Engine.Object.TargetFps.Should().Be(90);
    }

    /// <summary>Preferences saved before vsync and the cap existed keep their logging threshold and drop the old cap.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task EarlierPreferencesRestoreUncappedWithVSync()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var store = new EditorSettingsManager(fixture.Database);
        var context = SettingContext.Project(Path.TrimEndingDirectorySeparator(Path.GetFullPath(fixture.Project.ProjectRoot)).ToUpperInvariant());
        await store.SaveSettingAsync(
            new SettingKey<EarlierPreferences>("WorldEditor", "Preview"),
            new EarlierPreferences(fixture.Project.ProjectId, 60, 1),
            context).ConfigureAwait(false);

        // A later session reads the stored row, not the writer's typed cache.
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);

        _ = service.FrameRateCapEnabled.Should().BeFalse();
        _ = service.VSyncEnabled.Should().BeTrue();
        _ = service.LoggingVerbosity.Should().Be(1);
        _ = fixture.Engine.Object.TargetFps.Should().Be(0);
        _ = fixture.Results.Should().BeEmpty();
    }

    /// <summary>Switching projects restores each project's own preferences on the same running engine.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task ProjectSwitchKeepsPreferencesSeparate()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        var first = fixture.Project;
        await service.RestoreAsync(first).ConfigureAwait(false);
        service.FrameRateCapEnabled = true;
        service.FrameRateCap = 30;
        var second = Fixture.ProjectAt("Second");
        fixture.Projects.Activate(second);
        await service.RestoreAsync(second).ConfigureAwait(false);
        _ = service.FrameRateCapEnabled.Should().BeFalse();
        _ = fixture.Engine.Object.TargetFps.Should().Be(0);
        service.FrameRateCap = 50;
        fixture.Projects.Activate(first);
        await service.RestoreAsync(first).ConfigureAwait(false);
        _ = service.FrameRateCap.Should().Be(30);
        _ = fixture.Engine.Object.TargetFps.Should().Be(30);
    }

    /// <summary>A new project at a previously used path cannot inherit the old project's preferences.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task ReusedPathRequiresMatchingProjectIdentity()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        service.FrameRateCapEnabled = true;
        service.FrameRateCap = 15;
        await service.FlushAsync().ConfigureAwait(false);
        var replacement = Fixture.ProjectAt("First");
        fixture.Projects.Activate(replacement);
        await service.RestoreAsync(replacement).ConfigureAwait(false);
        _ = service.FrameRateCapEnabled.Should().BeFalse();
        _ = service.FrameRateCap.Should().Be(EngineConstants.DefaultFrameRateCap);
    }

    /// <summary>A late settings load cannot reactivate a retired workspace.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task RetiredWorkspaceDoesNotApplyDelayedLoad()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var store = new Mock<IEditorSettingsManager>();
        var loaded = new TaskCompletionSource<PreviewSettingsService.Preferences?>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = store.Setup(value => value.LoadSettingAsync(PreviewSettingsService.Key, It.IsAny<SettingContext>(), It.IsAny<IProgress<SettingsProgress>>(), It.IsAny<CancellationToken>())).Returns(loaded.Task);
        var service = fixture.CreateService(store.Object);
        var restoration = service.RestoreAsync(fixture.Project);
        service.Deactivate(fixture.Project);
        loaded.SetResult(new() { ProjectId = fixture.Project.ProjectId, FrameRateCapEnabled = true, FrameRateCap = 15, LoggingVerbosity = 3 });
        await restoration.ConfigureAwait(false);
        fixture.Engine.VerifySet(value => value.TargetFps = It.IsAny<uint>(), Times.Never());
        fixture.Engine.Verify(value => value.SetVSyncEnabled(It.IsAny<bool>()), Times.Never());
        _ = fixture.Engine.Object.EngineLoggingVerbosity.Should().Be(-2);
    }

    /// <summary>Persistence failure leaves the accepted live value and publishes the actual storage error.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task SaveFailureRemainsVisibleAndDoesNotPoisonLaterWrites()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var store = new Mock<IEditorSettingsManager>();
        _ = store.Setup(value => value.SaveSettingAsync(PreviewSettingsService.Key, It.IsAny<PreviewSettingsService.Preferences>(), It.IsAny<SettingContext>(), It.IsAny<IProgress<SettingsProgress>>(), It.IsAny<CancellationToken>()))
            .ThrowsAsync(new IOException("Preferences database is unavailable."));
        var service = fixture.CreateService(store.Object);
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        service.FrameRateCapEnabled = true;
        await service.FlushAsync().ConfigureAwait(false);
        _ = fixture.Engine.Object.TargetFps.Should().Be((uint)EngineConstants.DefaultFrameRateCap);
        _ = fixture.Results.Should().ContainSingle().Which.Diagnostics.Should()
            .ContainSingle(issue => issue.Code == DiagnosticCodes.SettingsPrefix + "SAVE_FAILED");

        _ = store.Setup(value => value.SaveSettingAsync(PreviewSettingsService.Key, It.IsAny<PreviewSettingsService.Preferences>(), It.IsAny<SettingContext>(), It.IsAny<IProgress<SettingsProgress>>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        service.FrameRateCap = 50;
        await service.FlushAsync().ConfigureAwait(false);
        _ = fixture.Engine.Object.TargetFps.Should().Be(50);
        _ = fixture.Results.Should().ContainSingle();
    }

    /// <summary>Closing waits for rapid toolbar changes to reach persistent storage in order.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task FlushPreservesTheLastAcceptedEdit()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        for (var fps = 20; fps <= 40; ++fps)
        {
            service.FrameRateCap = fps;
        }

        await service.FlushAsync().ConfigureAwait(false);
        var reopened = fixture.CreateService();
        await reopened.RestoreAsync(fixture.Project).ConfigureAwait(false);
        _ = reopened.FrameRateCap.Should().Be(40);
        _ = fixture.Results.Should().BeEmpty();
    }

    /// <summary>A slow store receives only the in-flight value and the latest value from a slider drag.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task SlowStoreCoalescesSliderChanges()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var store = new Mock<IEditorSettingsManager>();
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var writes = new List<int>();
        _ = store.Setup(value => value.SaveSettingAsync(PreviewSettingsService.Key, It.IsAny<PreviewSettingsService.Preferences>(), It.IsAny<SettingContext>(), It.IsAny<IProgress<SettingsProgress>>(), It.IsAny<CancellationToken>()))
            .Callback<SettingKey<PreviewSettingsService.Preferences>, PreviewSettingsService.Preferences, SettingContext?, IProgress<SettingsProgress>?, CancellationToken>(
                (_, value, _, _, _) => writes.Add(value.FrameRateCap))
            .Returns(() => writes.Count == 1 ? release.Task : Task.CompletedTask);
        var service = fixture.CreateService(store.Object);
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        for (var fps = 20; fps <= 50; ++fps)
        {
            service.FrameRateCap = fps;
        }

        _ = writes.Should().Equal(20);
        var closing = service.FlushAsync();
        _ = closing.IsCompleted.Should().BeFalse();
        release.SetResult();
        await closing.ConfigureAwait(false);
        _ = writes.Should().Equal(20, 50);
        service.FrameRateCap = 50;
        await service.FlushAsync().ConfigureAwait(false);
        _ = writes.Should().HaveCount(2);
    }

    /// <summary>Windows path casing and trailing separators do not change the preference identity.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task EquivalentProjectPathsRestoreTheSamePreferences()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        service.FrameRateCap = 45;
        await service.FlushAsync().ConfigureAwait(false);
        var sameProject = ProjectContext.FromProjectInfo(new ProjectInfo(
            fixture.Project.ProjectId,
            fixture.Project.Name,
            fixture.Project.Category,
            fixture.Project.ProjectRoot.ToUpperInvariant() + Path.DirectorySeparatorChar,
            fixture.Project.Thumbnail)
        {
            AuthoringMounts = [.. fixture.Project.AuthoringMounts],
        });
        fixture.Projects.Activate(sameProject);
        await service.RestoreAsync(sameProject).ConfigureAwait(false);
        _ = service.FrameRateCap.Should().Be(45);
    }

    /// <summary>The stored shape before vsync, the frame-rate cap and pane rendering were preferences.</summary>
    private sealed record EarlierPreferences(Guid ProjectId, int TargetFps, int LoggingVerbosity);

    private sealed class Fixture : IAsyncDisposable
    {
        private readonly PersistentState database = new(new DbContextOptionsBuilder<PersistentState>().UseSqlite("Data Source=:memory:").Options);
        private readonly Mock<IOperationResultPublisher> publisher = new();

        public Mock<IEngineService> Engine { get; } = new();

        public PersistentState Database => this.database;

        public ProjectContextService Projects { get; } = new();

        public ProjectContext Project { get; } = ProjectAt("First");

        public List<OperationResult> Results { get; } = [];

        public static async Task<Fixture> CreateAsync()
        {
            var fixture = new Fixture();
            await fixture.database.Database.OpenConnectionAsync().ConfigureAwait(false);
            _ = await fixture.database.Database.EnsureCreatedAsync().ConfigureAwait(false);
            fixture.Projects.Activate(fixture.Project);
            _ = fixture.Engine.SetupProperty(value => value.TargetFps, 0u);
            _ = fixture.Engine.SetupProperty(value => value.EngineLoggingVerbosity, -2);
            _ = fixture.Engine.SetupGet(value => value.MaxTargetFps).Returns(1000);
            _ = fixture.publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(fixture.Results.Add);
            return fixture;
        }

        public static ProjectContext ProjectAt(string name) => ProjectContext.FromProjectInfo(new ProjectInfo(
            name, Category.Games, Path.Combine(Path.GetTempPath(), "Oxygen.PreviewPreferences", name), string.Empty)
        {
            AuthoringMounts = [new("Content", "Content")],
        });

        public PreviewSettingsService CreateService(IEditorSettingsManager? store = null)
            => new(this.Engine.Object, store ?? new EditorSettingsManager(this.Database), this.Projects, this.publisher.Object, new OperationStatusReducer());

        public ValueTask DisposeAsync() => this.database.DisposeAsync();
    }
}
