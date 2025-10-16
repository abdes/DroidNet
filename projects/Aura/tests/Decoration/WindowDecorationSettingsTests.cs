// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.IO.Abstractions;
using System.Threading;
using System.Threading.Tasks;
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
public sealed class WindowDecorationSettingsTests
{
    [TestMethod]
    public void Constructor_LoadsInitialSettingsFromMonitor()
    {
        var defaults = WindowDecorationBuilder.ForPrimaryWindow().Build();
        var initial = new WindowDecorationSettings();
        initial.DefaultsByCategory[defaults.Category] = defaults;

        var harness = CreateHarness(initial);

        _ = harness.Service.GetDefaultForCategory("Primary").Should().Be(defaults);
    }

    [TestMethod]
    public void SetDefaultForCategory_NormalizesCategoryAndMarksDirty()
    {
        var harness = CreateHarness();
        var options = WindowDecorationBuilder.ForPrimaryWindow().Build();

        harness.Service.SetDefaultForCategory(" primary  ", options with { Category = "Other" });

        var stored = harness.Service.GetDefaultForCategory("Primary");
        stored.Should().NotBeNull();
        _ = stored!.Category.Should().Be("primary");
        _ = harness.Service.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public void RemoveDefaultForCategory_ReturnsFalseWhenMissing()
    {
        var harness = CreateHarness();

        var removed = harness.Service.RemoveDefaultForCategory("Unknown");

        _ = removed.Should().BeFalse();
    }

    [TestMethod]
    public void SetOverrideForType_ValidatesOptions()
    {
        var harness = CreateHarness();
        var invalid = WindowDecorationBuilder.ForPrimaryWindow().Build()
            with
        {
            Buttons = WindowButtonsOptions.Default with { ShowClose = false },
        };

        var act = () => harness.Service.SetOverrideForType("MyApp.Windows.MainWindow", invalid);

        _ = act.Should().Throw<ValidationException>();
    }

    [TestMethod]
    public async Task SaveAsync_PersistsSettingsToFile()
    {
        var harness = CreateHarness();
        harness.Service.SetDefaultForCategory("Primary", WindowDecorationBuilder.ForPrimaryWindow().Build());

        var result = await harness.Service.SaveAsync().ConfigureAwait(true);

        _ = result.Should().BeTrue();
        harness.FileMock.Verify(f => f.WriteAllText("C:/config/WindowDecorations.json", It.IsAny<string>()), Times.Once);
    }

    [TestMethod]
    public void OnChange_ReplacesStoredSettings()
    {
        var harness = CreateHarness();
        var newDefaults = WindowDecorationBuilder.ForSecondaryWindow().Build();
        var updated = new WindowDecorationSettings();
        updated.DefaultsByCategory[newDefaults.Category] = newDefaults;

        harness.TriggerChange(updated);
        Thread.Sleep(600);

        _ = harness.Service.GetDefaultForCategory("Secondary").Should().Be(newDefaults);
    }

    private static ServiceHarness CreateHarness(WindowDecorationSettings? initial = null)
    {
        var initialSettings = initial ?? new WindowDecorationSettings();

        var monitor = new Mock<IOptionsMonitor<WindowDecorationSettings>>();
        monitor.Setup(m => m.CurrentValue).Returns(initialSettings);

        Action<WindowDecorationSettings, string?>? onChange = null;
        monitor.Setup(m => m.OnChange(It.IsAny<Action<WindowDecorationSettings, string?>>()))
            .Callback<Action<WindowDecorationSettings, string?>>(handler => onChange = handler)
            .Returns(Mock.Of<IDisposable>());

        var fileSystem = new Mock<IFileSystem>();
        var file = new Mock<IFile>();
        var directory = new Mock<IDirectory>();
        var path = new Mock<IPath>();

        fileSystem.Setup(fs => fs.File).Returns(file.Object);
        fileSystem.Setup(fs => fs.Directory).Returns(directory.Object);
        fileSystem.Setup(fs => fs.Path).Returns(path.Object);

        var finder = new Mock<IPathFinder>();
        finder.Setup(f => f.GetConfigFilePath(WindowDecorationSettings.ConfigFileName))
            .Returns("C:/config/WindowDecorations.json");

        path.Setup(p => p.GetDirectoryName(It.IsAny<string>())).Returns("C:/config");
        directory.Setup(d => d.Exists(It.IsAny<string>())).Returns(true);
        file.Setup(f => f.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        var service = new WindowDecorationSettingsService(
            monitor.Object,
            fileSystem.Object,
            finder.Object,
            NullLoggerFactory.Instance);

        return new ServiceHarness(service, monitor, fileSystem, file, directory, path, finder, () => onChange);
    }

    private sealed class ServiceHarness
    {
        private readonly Func<Action<WindowDecorationSettings, string?>?> onChangeAccessor;

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
            this.Service = service;
            this.Monitor = monitor;
            this.FileSystemMock = fileSystem;
            this.FileMock = file;
            this.DirectoryMock = directory;
            this.PathMock = path;
            this.PathFinderMock = finder;
            this.onChangeAccessor = onChangeAccessor;
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
    }
}
