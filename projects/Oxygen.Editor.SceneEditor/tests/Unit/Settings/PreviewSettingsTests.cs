// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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

/// <summary>Checks project preference persistence independently of the native rendering loop.</summary>
[TestClass]
internal sealed class PreviewSettingsTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task PreferencesRoundTripThroughSqlite()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        _ = fixture.Engine.Object.TargetFps.Should().Be(0);
        fixture.Engine.Verify(value => value.SetVSyncEnabled(enabled: true), Times.Once());
        fixture.Engine.Verify(value => value.SetAlwaysRenderPanes(alwaysRender: false), Times.Once());
        _ = service.MaxFrameRateCap.Should().Be(1000);

        service.FrameRateCapEnabled = true;
        service.FrameRateCap = 45;
        service.VSyncEnabled = false;
        service.AlwaysRenderPanes = true;
        await service.FlushAsync().ConfigureAwait(false);
        _ = fixture.Engine.Object.TargetFps.Should().Be(45);
        fixture.Engine.Verify(value => value.SetVSyncEnabled(enabled: false), Times.Once());
        fixture.Engine.Verify(value => value.SetAlwaysRenderPanes(alwaysRender: true), Times.Once());

        var reopened = fixture.CreateService();
        await reopened.RestoreAsync(fixture.Project).ConfigureAwait(false);
        _ = reopened.FrameRateCapEnabled.Should().BeTrue();
        _ = reopened.FrameRateCap.Should().Be(45);
        _ = reopened.VSyncEnabled.Should().BeFalse();
        _ = reopened.AlwaysRenderPanes.Should().BeTrue();
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

    /// <summary>Preferences saved before vsync and the cap existed drop the old cap and logging threshold.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task EarlierPreferencesRestoreUncappedWithVSync()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var store = new EditorSettingsManager(fixture.Database);
        var context = SettingContext.Project(Path.TrimEndingDirectorySeparator(fixture.Project.ProjectRoot.FullPath).ToUpperInvariant());
        await store.SaveSettingAsync(
            new SettingKey<EarlierPreferences>("WorldEditor", "Preview"),
            new EarlierPreferences(fixture.Project.ProjectId, 60, 1),
            context,
            ct: this.TestContext.CancellationToken).ConfigureAwait(false);

        // A later session reads the stored row, not the writer's typed cache.
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);

        _ = service.FrameRateCapEnabled.Should().BeFalse();
        _ = service.VSyncEnabled.Should().BeTrue();
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
        loaded.SetResult(new() { ProjectId = fixture.Project.ProjectId, FrameRateCapEnabled = true, FrameRateCap = 15 });
        await restoration.ConfigureAwait(false);
        fixture.Engine.VerifySet(value => value.TargetFps = It.IsAny<uint>(), Times.Never());
        fixture.Engine.Verify(value => value.SetVSyncEnabled(It.IsAny<bool>()), Times.Never());
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
        var sameProject = fixture.Project with
        {
            ProjectRoot = FileSystemPath.From(fixture.Project.ProjectRoot.FullPath.ToUpperInvariant() + Path.DirectorySeparatorChar),
        };
        fixture.Projects.Activate(sameProject);
        await service.RestoreAsync(sameProject).ConfigureAwait(false);
        _ = service.FrameRateCap.Should().Be(45);
    }

    /// <summary>
    ///     The ground grid starts at the engine defaults, every edit reaches the engine, and the grid is the user's:
    ///     another project restores it too.
    /// </summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task GroundGridIsSavedForTheUserAcrossProjects()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        fixture.Engine.Verify(value => value.SetGroundGrid(new GroundGridSettings()), Times.Once());

        service.GroundGrid.Spacing = 2.5f;
        service.GroundGrid.MajorEvery = 4;
        service.GroundGrid.SmoothMotion = false;
        service.GroundGrid.MinorColor = Windows.UI.Color.FromArgb(255, 255, 0, 0);
        await service.FlushAsync().ConfigureAwait(false);
        var edited = new GroundGridSettings
        {
            Spacing = 2.5f,
            MajorEvery = 4,
            SmoothMotion = false,
            MinorColor = new(1f, 0f, 0f, 1f),
        };
        fixture.Engine.Verify(value => value.SetGroundGrid(edited), Times.Once());

        var second = Fixture.ProjectAt("Second");
        fixture.Projects.Activate(second);
        var reopened = fixture.CreateService();
        await reopened.RestoreAsync(second).ConfigureAwait(false);
        _ = reopened.GroundGrid.Spacing.Should().Be(2.5f);
        _ = reopened.GroundGrid.MajorEvery.Should().Be(4);
        _ = reopened.GroundGrid.SmoothMotion.Should().BeFalse();
        _ = reopened.GroundGrid.MinorColor.Should().Be(Windows.UI.Color.FromArgb(255, 255, 0, 0));
        fixture.Engine.Verify(value => value.SetGroundGrid(edited), Times.Exactly(2));

        reopened.GroundGrid.ResetToDefaultsCommand.Execute(parameter: null);
        _ = reopened.GroundGrid.Spacing.Should().Be(1f);
        fixture.Engine.Verify(value => value.SetGroundGrid(new GroundGridSettings()), Times.Exactly(2));
        _ = fixture.Results.Should().BeEmpty();
    }

    /// <summary>An edit outside the engine's range is clamped; one that is not a number keeps the current value.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task GroundGridEditsAreClampedToTheEngineRanges()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);

        service.GroundGrid.Spacing = -3f;
        service.GroundGrid.MajorEvery = 0;
        service.GroundGrid.LineThickness = -0.5f;
        service.GroundGrid.SmoothTime = 0f;
        service.GroundGrid.FadePower = float.NaN;

        _ = service.GroundGrid.Spacing.Should().Be(GroundGridRanges.MinSpacing);
        _ = service.GroundGrid.MajorEvery.Should().Be(1);
        _ = service.GroundGrid.LineThickness.Should().Be(0f);
        _ = service.GroundGrid.SmoothTime.Should().Be(GroundGridRanges.MinSmoothTime);
        _ = service.GroundGrid.FadePower.Should().Be(2f);
    }

    /// <summary>A stored grid value outside its range restores its default; the valid ones are kept.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task UnusableStoredGroundGridValuesRestoreDefaults()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var store = new EditorSettingsManager(fixture.Database);
        await store.SaveSettingAsync(
            PreviewSettingsService.GroundGridKey,
            new GroundGridSettings { Spacing = -1f, MajorEvery = 0, HorizonBoost = 0.8f, AxisColorX = new(2f, 0f, 0f, 1f) },
            SettingContext.Application(),
            ct: this.TestContext.CancellationToken).ConfigureAwait(false);

        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);

        var restored = new GroundGridSettings { HorizonBoost = 0.8f };
        fixture.Engine.Verify(value => value.SetGroundGrid(restored), Times.Once());
        _ = service.GroundGrid.Spacing.Should().Be(1f);
        _ = service.GroundGrid.HorizonBoost.Should().Be(0.8f);
        _ = fixture.Results.Should().BeEmpty();
    }

    /// <summary>
    ///     A color picker showing the stored color as 8-bit channels writes it back unchanged, which keeps the precise
    ///     stored channels and saves nothing.
    /// </summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task UnchangedPickerColorKeepsTheStoredChannels()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);

        service.GroundGrid.MinorColor = service.GroundGrid.MinorColor;
        await service.FlushAsync().ConfigureAwait(false);

        fixture.Engine.Verify(value => value.SetGroundGrid(It.IsAny<GroundGridSettings>()), Times.Once());
        var reopened = fixture.CreateService();
        await reopened.RestoreAsync(fixture.Project).ConfigureAwait(false);
        fixture.Engine.Verify(value => value.SetGroundGrid(new GroundGridSettings()), Times.Exactly(2));
    }

    /// <summary>An engine that does not run takes no preference; it takes the accepted ones when it starts running.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task PreferencesWaitForARunningEngine()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        fixture.EngineState = EngineServiceState.NoEngine;
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        service.FrameRateCapEnabled = true;
        service.FrameRateCap = 45;
        service.GroundGrid.Spacing = 2f;
        await service.FlushAsync().ConfigureAwait(false);

        fixture.Engine.VerifySet(value => value.TargetFps = It.IsAny<uint>(), Times.Never());
        fixture.Engine.Verify(value => value.SetVSyncEnabled(It.IsAny<bool>()), Times.Never());
        fixture.Engine.Verify(value => value.SetGroundGrid(It.IsAny<GroundGridSettings>()), Times.Never());
        _ = fixture.Results.Should().BeEmpty("an engine that does not run rejects nothing");

        fixture.ChangeState(EngineServiceState.Running);

        _ = fixture.Engine.Object.TargetFps.Should().Be(45);
        fixture.Engine.Verify(value => value.SetVSyncEnabled(enabled: true), Times.Once());
        fixture.Engine.Verify(value => value.SetGroundGrid(new GroundGridSettings { Spacing = 2f }), Times.Once());
    }

    /// <summary>An engine that stops and starts again takes the accepted preferences again.</summary>
    /// <returns>The verification task.</returns>
    [TestMethod]
    public async Task RestartedEngineTakesThePreferencesAgain()
    {
        var fixture = await Fixture.CreateAsync().ConfigureAwait(false);
        await using var lifetime = fixture.ConfigureAwait(false);
        var service = fixture.CreateService();
        await service.RestoreAsync(fixture.Project).ConfigureAwait(false);
        service.VSyncEnabled = false;
        fixture.ChangeState(EngineServiceState.NoEngine);
        fixture.Engine.Invocations.Clear();

        fixture.ChangeState(EngineServiceState.Running);

        fixture.Engine.Verify(value => value.SetVSyncEnabled(enabled: false), Times.Once());
        fixture.Engine.Verify(value => value.SetGroundGrid(It.IsAny<GroundGridSettings>()), Times.Once());
        _ = fixture.Results.Should().BeEmpty();
    }

    /// <summary>The stored shape before vsync, the frame-rate cap and pane rendering were preferences.</summary>
    private sealed record EarlierPreferences(Guid ProjectId, int TargetFps, int LoggingVerbosity);

    private sealed class Fixture : IAsyncDisposable
    {
        private readonly Mock<IOperationResultPublisher> publisher = new();

        public Mock<IEngineService> Engine { get; } = new();

        /// <summary>Gets or sets the engine's state; it runs unless a test stops it.</summary>
        public EngineServiceState EngineState { get; set; } = EngineServiceState.Running;

        public PersistentState Database { get; } = new(new DbContextOptionsBuilder<PersistentState>().UseSqlite("Data Source=:memory:").Options);

        public ProjectContextService Projects { get; } = new();

        public ProjectContext Project { get; } = ProjectAt("First");

        public List<OperationResult> Results { get; } = [];

        public static async Task<Fixture> CreateAsync()
        {
            var fixture = new Fixture();
            await fixture.Database.Database.OpenConnectionAsync().ConfigureAwait(false);
            _ = await fixture.Database.Database.EnsureCreatedAsync().ConfigureAwait(false);
            fixture.Projects.Activate(fixture.Project);
            _ = fixture.Engine.SetupProperty(value => value.TargetFps, 0u);
            _ = fixture.Engine.SetupGet(value => value.MaxTargetFps).Returns(1000);
            _ = fixture.Engine.SetupGet(value => value.State).Returns(() => fixture.EngineState);
            _ = fixture.publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(fixture.Results.Add);
            return fixture;
        }

        public static ProjectContext ProjectAt(string name)
            => TestProject.Create(new MockFileSystem(), Path.Combine(Path.GetTempPath(), "Oxygen.PreviewPreferences", name)).Context with { Name = name };

        public PreviewSettingsService CreateService(IEditorSettingsManager? store = null)
            => new(this.Engine.Object, store ?? new EditorSettingsManager(this.Database), this.Projects, this.publisher.Object, new OperationStatusReducer());

        /// <summary>Moves the engine to another state, as the engine service reports it.</summary>
        /// <param name="state">The new state.</param>
        public void ChangeState(EngineServiceState state)
        {
            var previous = this.EngineState;
            this.EngineState = state;
            this.Engine.Raise(value => value.StateChanged += null, this.Engine.Object, new EngineStateChangedEventArgs(Guid.NewGuid(), previous, state));
        }

        public ValueTask DisposeAsync() => this.Database.DisposeAsync();
    }
}
