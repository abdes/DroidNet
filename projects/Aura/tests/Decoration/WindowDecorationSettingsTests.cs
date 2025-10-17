// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using DroidNet.Aura.Decoration;
using DroidNet.Config;
using FluentAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Moq;

namespace DroidNet.Aura.Tests.Decoration;

/// <summary>
/// Unit tests for <see cref="WindowDecorationSettingsService"/>.
/// </summary>
[TestClass]
[TestCategory("Window Decoration Settings")]
public sealed partial class WindowDecorationSettingsTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public void Constructor_LoadsInitialSettingsFromMonitor()
    {
        var categoryOverride = WindowDecorationBuilder.ForMainWindow().Build();
        var initial = new WindowDecorationSettings();
        initial.CategoryOverrides[categoryOverride.Category] = categoryOverride;

        using var harness = CreateHarness(initial);

        _ = harness.Service.CategoryOverrides.Should().ContainKey(WindowCategory.Main);
        _ = harness.Service.CategoryOverrides[WindowCategory.Main].Should().Be(categoryOverride);
    }

    [TestMethod]
    public void SetCategoryOverride_NormalizesCategoryAndMarksDirty()
    {
        using var harness = CreateHarness();
        var options = WindowDecorationBuilder.ForMainWindow().Build();

        harness.Service.SetCategoryOverride(new(" primary  "), options with { Category = new("Other") });

        _ = harness.Service.CategoryOverrides.Should().ContainKey(new("primary"));
        _ = harness.Service.CategoryOverrides[new("primary")].Category.Should().Be(new WindowCategory("primary"));
        _ = harness.Service.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public void RemoveCategoryOverride_ReturnsFalseWhenMissing()
    {
        using var harness = CreateHarness();

        var removed = harness.Service.RemoveCategoryOverride(new("CustomCategory"));

        _ = removed.Should().BeFalse();
    }

    [TestMethod]
    public void SetCategoryOverride_ValidatesOptions()
    {
        using var harness = CreateHarness();
        var invalid = WindowDecorationBuilder.ForMainWindow().Build()
            with
        {
            Buttons = WindowButtonsOptions.Default with { ShowClose = false },
        };

        var act = () => harness.Service.SetCategoryOverride(new("Main"), invalid);

        _ = act.Should().Throw<ValidationException>();
    }

    [TestMethod]
    public async Task SaveAsync_PersistsSettingsToFile()
    {
        using var harness = CreateHarness();
        harness.Service.SetCategoryOverride(new("Main"), WindowDecorationBuilder.ForMainWindow().Build());

        var result = await harness.Service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.Should().BeTrue();
        harness.FileMock.Verify(f => f.WriteAllText("C:/config/WindowDecorations.json", It.IsAny<string>()), Times.Once);
    }

    [TestMethod]
    public void OnChange_ReplacesStoredSettings()
    {
        using var harness = CreateHarness();
        var newOverride = WindowDecorationBuilder.ForSecondaryWindow().Build();
        var updated = new WindowDecorationSettings();
        updated.CategoryOverrides[newOverride.Category] = newOverride;

        harness.TriggerChange(updated);
        Thread.Sleep(600);

        _ = harness.Service.CategoryOverrides.Should().ContainKey(new("Secondary"));
        _ = harness.Service.CategoryOverrides[new("Secondary")].Should().Be(newOverride);
    }

    [TestMethod]
    public void GetEffectiveDecoration_ReturnsCodeDefinedDefaultWhenNoOverride()
    {
        using var harness = CreateHarness();

        var effective = harness.Service.GetEffectiveDecoration(WindowCategory.Main);

        _ = effective.Should().NotBeNull();
        _ = effective.Category.Should().Be(WindowCategory.Main);
        _ = effective.ChromeEnabled.Should().BeTrue();
        _ = effective.TitleBar.Height.Should().Be(40.0);
        _ = effective.Backdrop.Should().Be(BackdropKind.MicaAlt);
    }

    [TestMethod]
    public void GetEffectiveDecoration_ReturnsOverrideWhenPresent()
    {
        using var harness = CreateHarness();
        var customOptions = new WindowDecorationOptions
        {
            Category = WindowCategory.Main,
            ChromeEnabled = true,
            TitleBar = TitleBarOptions.Default with { Height = 50.0 },
            Buttons = WindowButtonsOptions.Default,
            Backdrop = BackdropKind.Acrylic,
        };
        harness.Service.SetCategoryOverride(WindowCategory.Main, customOptions);

        var effective = harness.Service.GetEffectiveDecoration(WindowCategory.Main);

        _ = effective.Should().Be(customOptions);
        _ = effective.TitleBar.Height.Should().Be(50.0);
        _ = effective.Backdrop.Should().Be(BackdropKind.Acrylic);
    }

    [TestMethod]
    public void GetEffectiveDecoration_FallbacksToSystemForUnrecognizedCategory()
    {
        using var harness = CreateHarness();

        var effective = harness.Service.GetEffectiveDecoration(new("NonExistentCategory"));

        _ = effective.Should().NotBeNull();
        _ = effective.Category.Should().Be(WindowCategory.System);
    }

    [TestMethod]
    public void GetEffectiveDecoration_IsCaseInsensitive()
    {
        using var harness = CreateHarness();

        var effective1 = harness.Service.GetEffectiveDecoration(new("main"));
        var effective2 = harness.Service.GetEffectiveDecoration(new("MAIN"));
        var effective3 = harness.Service.GetEffectiveDecoration(new("Main"));

        _ = effective1.Should().Be(effective2);
        _ = effective2.Should().Be(effective3);
    }

    private static ServiceHarness CreateHarness(WindowDecorationSettings? initial = null)
    {
        var initialSettings = initial ?? new WindowDecorationSettings();

        var monitor = new Mock<IOptionsMonitor<WindowDecorationSettings>>();
        _ = monitor.Setup(m => m.CurrentValue).Returns(initialSettings);

        Action<WindowDecorationSettings, string?>? onChange = null;
        _ = monitor.Setup(m => m.OnChange(It.IsAny<Action<WindowDecorationSettings, string?>>()))
            .Callback<Action<WindowDecorationSettings, string?>>(handler => onChange = handler)
            .Returns(Mock.Of<IDisposable>());

        var fileSystem = new Mock<IFileSystem>();
        var file = new Mock<IFile>();
        var directory = new Mock<IDirectory>();
        var path = new Mock<IPath>();

        _ = fileSystem.Setup(fs => fs.File).Returns(file.Object);
        _ = fileSystem.Setup(fs => fs.Directory).Returns(directory.Object);
        _ = fileSystem.Setup(fs => fs.Path).Returns(path.Object);

        var finder = new Mock<IPathFinder>();
        _ = finder.Setup(f => f.GetConfigFilePath(WindowDecorationSettings.ConfigFileName))
            .Returns("C:/config/WindowDecorations.json");

        _ = path.Setup(p => p.GetDirectoryName(It.IsAny<string>())).Returns("C:/config");
        _ = directory.Setup(d => d.Exists(It.IsAny<string>())).Returns(value: true);
        _ = file.Setup(f => f.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        var service = new WindowDecorationSettingsService(
            monitor.Object,
            fileSystem.Object,
            finder.Object,
            NullLoggerFactory.Instance);

        return new ServiceHarness(service, monitor, fileSystem, file, directory, path, finder, () => onChange);
    }

    private sealed partial class ServiceHarness : IDisposable
    {
        private readonly Func<Action<WindowDecorationSettings, string?>?> onChangeAccessor;

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "ownership of disposable service should be clear to analyzers")]
        public ServiceHarness(
            WindowDecorationSettingsService service,
            Mock<IOptionsMonitor<WindowDecorationSettings>> monitor,
            Mock<IFileSystem> fileSystem,
            Mock<IFile> file,
            Mock<IDirectory> directory,
            Mock<IPath> path,
            Mock<IPathFinder> finder,
            Func<Action<WindowDecorationSettings, string?>?> onChangeAccessor)
        {
            this.onChangeAccessor = onChangeAccessor;
            this.Service = service;
            this.Monitor = monitor;
            this.FileSystemMock = fileSystem;
            this.FileMock = file;
            this.DirectoryMock = directory;
            this.PathMock = path;
            this.PathFinderMock = finder;
        }

        public WindowDecorationSettingsService Service { get; }

        public Mock<IOptionsMonitor<WindowDecorationSettings>> Monitor { get; }

        public Mock<IFileSystem> FileSystemMock { get; }

        public Mock<IFile> FileMock { get; }

        public Mock<IDirectory> DirectoryMock { get; }

        public Mock<IPath> PathMock { get; }

        public Mock<IPathFinder> PathFinderMock { get; }

        public void TriggerChange(WindowDecorationSettings settings)
        {
            var handler = this.onChangeAccessor();
            handler?.Invoke(settings, null);
        }

        public void Dispose()
        {
            this.Service.Dispose();
            GC.SuppressFinalize(this);
        }
    }
}
