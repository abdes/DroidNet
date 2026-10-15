// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Runtime.Player;
using Oxygen.Editor.World.Play;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Play;

/// <summary>The per-user player launch settings: display mode, window size, VSync and frame rate limit.</summary>
[TestClass]
[TestCategory("Play")]
public sealed class PlaySessionSettingsTests
{
    private static PlayerWindowSizeOption FullHd => PlaySessionSettingsService.SizeOptions.Single(option => option.Name == "Full HD");

    [TestMethod]
    public void Settings_StartFromTheDefaultsAndIgnoreValuesThePlayerRejects()
    {
        var sut = new PlaySessionSettingsService(settings: null)
        {
            Width = 32,
            Height = 20000,
            FrameRateLimit = 241,
        };

        _ = (sut.WindowMode, sut.Width, sut.Height, sut.SizeOption.Name, sut.VSync, sut.FrameRateLimited, sut.FrameRateLimit, sut.FrameCap)
            .Should().Be((PlayerWindowMode.Windowed, 1280, 720, "HD", true, false, 60, 0));
    }

    [TestMethod]
    public void FrameCap_IsTheLimitOnlyWhileTheFrameRateIsLimited()
    {
        var sut = new PlaySessionSettingsService(settings: null) { FrameRateLimit = 30 };
        var changed = new List<string?>();
        sut.PropertyChanged += (_, args) => changed.Add(args.PropertyName);

        sut.FrameRateLimited = true;
        var limited = sut.FrameCap;
        sut.FrameRateLimited = false;

        _ = (limited, sut.FrameCap, sut.FrameRateLimit).Should().Be((30, 0, 30), "turning the limit off keeps its value for the next time");
        _ = changed.Should().Equal(
            nameof(PlaySessionSettingsService.FrameRateLimited),
            nameof(PlaySessionSettingsService.FrameCap),
            nameof(PlaySessionSettingsService.FrameRateLimited),
            nameof(PlaySessionSettingsService.FrameCap));
    }

    [TestMethod]
    public void SizeOption_NamedSizesSetTheSizeAndCustomKeepsItEditable()
    {
        var sut = new PlaySessionSettingsService(settings: null);

        sut.SizeOption = FullHd;
        var named = (sut.Width, sut.Height, sut.HasCustomSize);
        sut.SizeOption = PlayerWindowSizeOption.Custom;
        var custom = (sut.SizeOption, sut.HasCustomSize, sut.Width);
        sut.Width = 1000;

        _ = named.Should().Be((1920, 1080, false));
        _ = custom.Should().Be((PlayerWindowSizeOption.Custom, true, 1920), "Custom keeps the size, even one with a name");
        _ = (sut.SizeOption, sut.Width, sut.Height).Should().Be((PlayerWindowSizeOption.Custom, 1000, 1080));
        _ = FullHd.Label.Should().Be("1920 × 1080 (Full HD)");
        _ = PlayerWindowSizeOption.Custom.Label.Should().Be("Custom…");
    }

    [TestMethod]
    public void Borderless_LeavesTheSizeToTheDisplayAndResetRestoresEveryDefault()
    {
        var sut = new PlaySessionSettingsService(settings: null) { SizeOption = PlayerWindowSizeOption.Custom, Width = 900, VSync = false };

        sut.WindowMode = PlayerWindowMode.Borderless;
        var borderless = sut.IsWindowed;
        sut.Reset();

        _ = borderless.Should().BeFalse();
        _ = (sut.WindowMode, sut.IsWindowed, sut.Width, sut.HasCustomSize, sut.VSync).Should().Be((PlayerWindowMode.Windowed, true, 1280, false, true));
    }

    [TestMethod]
    public async Task Settings_PersistForTheUserAcrossProjects()
    {
        var store = new Mock<IEditorSettingsManager>();
        var sut = new PlaySessionSettingsService(store.Object)
        {
            WindowMode = PlayerWindowMode.Borderless,
            SizeOption = FullHd,
            FrameRateLimited = true,
            FrameRateLimit = 90,
        };

        await sut.FlushAsync().ConfigureAwait(false);

        var expected = new PlaySessionSettingsService.Preferences(
            PlaySessionSettingsService.CurrentVersion, PlayerWindowMode.Borderless, 1920, 1080, CustomSize: false, VSync: true, FrameRateLimited: true, FrameRateLimit: 90);
        store.Verify(
            manager => manager.SaveSettingAsync(
                PlaySessionSettingsService.Key,
                It.Is<PlaySessionSettingsService.Preferences>(stored => stored == expected),
                It.Is<SettingContext?>(context => context != null && context.Scope == SettingScope.Application),
                It.IsAny<IProgress<SettingsProgress>?>(),
                It.IsAny<CancellationToken>()),
            Times.Once());
    }

    [TestMethod]
    public async Task Settings_RestoreStoredPreferencesWithoutWritingThemBack()
    {
        var store = Store(PlaySessionSettingsService.Defaults with { Width = 2560, Height = 1440, VSync = false, FrameRateLimited = true, FrameRateLimit = 120 });
        var sut = new PlaySessionSettingsService(store.Object);

        await sut.EnsureLoadedAsync().ConfigureAwait(false);
        await sut.FlushAsync().ConfigureAwait(false);

        _ = (sut.Width, sut.Height, sut.VSync, sut.FrameCap, sut.SizeOption.Name).Should().Be((2560, 1440, false, 120, "QHD"));
        store.Verify(
            manager => manager.SaveSettingAsync(
                It.IsAny<SettingKey<PlaySessionSettingsService.Preferences>>(),
                It.IsAny<PlaySessionSettingsService.Preferences>(),
                It.IsAny<SettingContext?>(),
                It.IsAny<IProgress<SettingsProgress>?>(),
                It.IsAny<CancellationToken>()),
            Times.Never);
    }

    [TestMethod]
    public async Task Settings_DiscardAnUnknownVersionAndOutOfRangeValues()
    {
        var unknown = new PlaySessionSettingsService(Store(PlaySessionSettingsService.Defaults with { Version = PlaySessionSettingsService.CurrentVersion - 1, Width = 1600 }).Object);
        var outOfRange = new PlaySessionSettingsService(Store(PlaySessionSettingsService.Defaults with { Width = 10, Height = 900, FrameRateLimit = 999 }).Object);

        await unknown.EnsureLoadedAsync().ConfigureAwait(false);
        await outOfRange.EnsureLoadedAsync().ConfigureAwait(false);

        _ = unknown.Width.Should().Be(1280);
        _ = (outOfRange.Width, outOfRange.Height, outOfRange.FrameRateLimit, outOfRange.HasCustomSize).Should().Be((1280, 900, 60, true), "a size no entry names reads as Custom");
    }

    private static Mock<IEditorSettingsManager> Store(PlaySessionSettingsService.Preferences stored)
    {
        var store = new Mock<IEditorSettingsManager>();
        _ = store
            .Setup(manager => manager.LoadSettingAsync(PlaySessionSettingsService.Key, It.IsAny<SettingContext?>(), It.IsAny<IProgress<SettingsProgress>?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(stored);
        return store;
    }
}
