// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls.OutputConsole.Model;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Output.Unit.Tests;

/// <summary>The Output panel keeps the user's view preferences across sessions; search, follow and pause are session state.</summary>
[TestClass]
public sealed class OutputViewModelTests
{
    private static readonly SubsystemMap NoSubsystems = new([]);

    /// <summary>Each view preference change is written for the user, application-wide.</summary>
    [TestMethod]
    public async Task ViewPreferences_AreSavedForTheUser()
    {
        var store = new Mock<IEditorSettingsManager>();
        var sut = new OutputViewModel(new OutputLogBuffer(NoSubsystems), store.Object);
        await sut.Loading.ConfigureAwait(false);

        sut.LevelFilter = LevelMask.Warning | LevelMask.Error;
        sut.HiddenSubsystems = ["Engine"];
        sut.ShowTimestamps = true;
        sut.ShowSubsystem = false;
        sut.WordWrap = true;
        sut.FoldDuplicates = true;
        sut.SearchMode = OutputSearchMode.Find;
        sut.IsDetailsPaneOpen = true;
        sut.DetailsSplitRatio = 0.7;
        await sut.FlushAsync().ConfigureAwait(false);

        store.Verify(
            manager => manager.SaveSettingAsync(
                OutputViewModel.Key,
                It.Is<OutputViewModel.Preferences>(stored =>
                    stored.Version == OutputViewModel.CurrentVersion
                    && stored.Levels == (LevelMask.Warning | LevelMask.Error)
                    && stored.HiddenSubsystems.SequenceEqual(new[] { "Engine" })
                    && stored.ShowTimestamps
                    && !stored.ShowSubsystem
                    && stored.WordWrap
                    && stored.FoldDuplicates
                    && stored.SearchMode == OutputSearchMode.Find
                    && stored.DetailsPaneOpen
                    && stored.DetailsSplitRatio == 0.7),
                It.Is<SettingContext?>(context => context != null && context.Scope == SettingScope.Application),
                It.IsAny<IProgress<SettingsProgress>?>(),
                It.IsAny<CancellationToken>()),
            Times.Once);
    }

    /// <summary>Stored preferences are shown when the panel's view model is created, without being written back.</summary>
    [TestMethod]
    public async Task StoredPreferences_AreLoaded()
    {
        var stored = new OutputViewModel.Preferences(
            OutputViewModel.CurrentVersion,
            LevelMask.Error,
            ["Cooker", string.Empty],
            ShowTimestamps: true,
            ShowSubsystem: false,
            WordWrap: true,
            FoldDuplicates: true,
            OutputSearchMode.Find,
            DetailsPaneOpen: true,
            DetailsSplitRatio: 0.45);
        var store = new Mock<IEditorSettingsManager>();
        _ = store
            .Setup(manager => manager.LoadSettingAsync(OutputViewModel.Key, It.IsAny<SettingContext?>(), It.IsAny<IProgress<SettingsProgress>?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(stored);

        var sut = new OutputViewModel(new OutputLogBuffer(NoSubsystems), store.Object);
        await sut.Loading.ConfigureAwait(false);
        await sut.FlushAsync().ConfigureAwait(false);

        _ = sut.LevelFilter.Should().Be(LevelMask.Error);
        _ = sut.HiddenSubsystems.Should().Equal("Cooker", string.Empty);
        _ = (sut.ShowTimestamps, sut.ShowSubsystem, sut.WordWrap, sut.FoldDuplicates, sut.SearchMode, sut.IsDetailsPaneOpen)
            .Should().Be((true, false, true, true, OutputSearchMode.Find, true));
        _ = sut.DetailsSplitRatio.Should().Be(0.45);
        store.Verify(
            manager => manager.SaveSettingAsync(
                It.IsAny<SettingKey<OutputViewModel.Preferences>>(),
                It.IsAny<OutputViewModel.Preferences>(),
                It.IsAny<SettingContext?>(),
                It.IsAny<IProgress<SettingsProgress>?>(),
                It.IsAny<CancellationToken>()),
            Times.Never);
    }

    /// <summary>Preferences of another payload version are discarded; the defaults apply.</summary>
    [TestMethod]
    public async Task StoredPreferences_OfAnotherVersion_AreDiscarded()
    {
        var stored = new OutputViewModel.Preferences(
            OutputViewModel.CurrentVersion + 1,
            LevelMask.Error,
            [],
            ShowTimestamps: true,
            ShowSubsystem: false,
            WordWrap: true,
            FoldDuplicates: true,
            OutputSearchMode.Find,
            DetailsPaneOpen: true,
            DetailsSplitRatio: 0.45);
        var store = new Mock<IEditorSettingsManager>();
        _ = store
            .Setup(manager => manager.LoadSettingAsync(OutputViewModel.Key, It.IsAny<SettingContext?>(), It.IsAny<IProgress<SettingsProgress>?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(stored);

        var sut = new OutputViewModel(new OutputLogBuffer(NoSubsystems), store.Object);
        await sut.Loading.ConfigureAwait(false);

        _ = sut.LevelFilter.Should().Be(LevelMask.Information | LevelMask.Warning | LevelMask.Error | LevelMask.Fatal);
        _ = sut.ShowTimestamps.Should().BeFalse();
    }

    /// <summary>The search text, following and pausing are session state and are never written.</summary>
    [TestMethod]
    public async Task SessionState_IsNotSaved()
    {
        var store = new Mock<IEditorSettingsManager>();
        var sut = new OutputViewModel(new OutputLogBuffer(NoSubsystems), store.Object);
        await sut.Loading.ConfigureAwait(false);

        sut.TextFilter = "cook";
        sut.FollowTail = false;
        sut.IsPaused = true;
        await sut.FlushAsync().ConfigureAwait(false);

        store.Verify(
            manager => manager.SaveSettingAsync(
                It.IsAny<SettingKey<OutputViewModel.Preferences>>(),
                It.IsAny<OutputViewModel.Preferences>(),
                It.IsAny<SettingContext?>(),
                It.IsAny<IProgress<SettingsProgress>?>(),
                It.IsAny<CancellationToken>()),
            Times.Never);
    }
}
