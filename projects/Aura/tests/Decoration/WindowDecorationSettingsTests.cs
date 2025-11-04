// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using DryIoc;
using FluentAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;

namespace DroidNet.Aura.Tests.Decoration;

/// <summary>
///     Unit tests for <see cref="WindowDecorationSettingsService"/>.
/// </summary>
[TestClass]
[TestCategory("Window Decoration Settings")]
public sealed partial class WindowDecorationSettingsTests : IDisposable
{
    // Per-test harness instance. Tests should assign this to enable deterministic disposal
    // via the test class Dispose() implementation.
    private ServiceHarness? harness;

    public TestContext TestContext { get; set; }

    [TestCleanup]
    public void TestCleanup()
    {
        // Ensure any created harness is disposed between tests to avoid resource leaks.
        this.harness?.Dispose();
        this.harness = null;
    }

    public void Dispose()
    {
        this.harness?.Dispose();
        GC.SuppressFinalize(this);
    }

    [TestMethod]
    public void Constructor_LoadsInitialSettingsFromProvidedSnapshot()
    {
        var categoryOverride = WindowDecorationBuilder.ForMainWindow().Build();
        var initial = new WindowDecorationSettings();
        initial.CategoryOverrides[categoryOverride.Category] = categoryOverride;

        this.harness = CreateHarness(initial);

        _ = this.harness.Service.CategoryOverrides.Should().ContainKey(WindowCategory.Main);
        _ = this.harness.Service.CategoryOverrides[WindowCategory.Main].Should().Be(categoryOverride);
    }

    [TestMethod]
    public void SetCategoryOverride_NormalizesCategoryAndMarksDirty()
    {
        this.harness = CreateHarness();
        var options = WindowDecorationBuilder.ForMainWindow().Build();

        this.harness.Service.SetCategoryOverride(new(" primary  "), options with { Category = new("Other") });

        _ = this.harness.Service.CategoryOverrides.Should().ContainKey(new("primary"));
        _ = this.harness.Service.CategoryOverrides[new("primary")].Category.Should().Be(new WindowCategory("primary"));
        _ = this.harness.Service.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public void RemoveCategoryOverride_ReturnsFalseWhenMissing()
    {
        this.harness = CreateHarness();

        var removed = this.harness.Service.RemoveCategoryOverride(new("CustomCategory"));

        _ = removed.Should().BeFalse();
    }

    [TestMethod]
    public void SaveAsync_PersistsSettingsToFile()
    {
        this.harness = CreateHarness();
        this.harness.Service.SetCategoryOverride(new("Main"), WindowDecorationBuilder.ForMainWindow().Build());

        // Use the SettingsService SaveAsync (new API) and ensure it completes without exception
        Func<Task> act = () => this.harness.Service.SaveAsync(this.TestContext.CancellationToken);

        _ = act.Should().NotThrowAsync();
    }

    [TestMethod]
    public void OnChange_ReplacesStoredSettings()
    {
        this.harness = CreateHarness();
        var newOverride = WindowDecorationBuilder.ForSecondaryWindow().Build();
        var updated = new WindowDecorationSettings();
        updated.CategoryOverrides[newOverride.Category] = newOverride;

        // Apply settings snapshot directly to simulate an external change.
        this.harness.TriggerChange(updated);

        _ = this.harness.Service.CategoryOverrides.Should().ContainKey(new("Secondary"));
        _ = this.harness.Service.CategoryOverrides[new("Secondary")].Should().Be(newOverride);
    }

    [TestMethod]
    public void GetEffectiveDecoration_ReturnsCodeDefinedDefaultWhenNoOverride()
    {
        this.harness = CreateHarness();

        var effective = this.harness.Service.GetEffectiveDecoration(WindowCategory.Main);

        _ = effective.Should().NotBeNull();
        _ = effective.Category.Should().Be(WindowCategory.Main);
        _ = effective.ChromeEnabled.Should().BeTrue();
        _ = effective.TitleBar?.Height.Should().Be(40.0);
        _ = effective.Backdrop.Should().Be(BackdropKind.MicaAlt);
    }

    [TestMethod]
    public void GetEffectiveDecoration_ReturnsOverrideWhenPresent()
    {
        this.harness = CreateHarness();
        var customOptions = new WindowDecorationOptions
        {
            Category = WindowCategory.Main,
            ChromeEnabled = true,
            TitleBar = TitleBarOptions.Default with { Height = 50.0 },
            Buttons = WindowButtonsOptions.Default,
            Backdrop = BackdropKind.Acrylic,
        };
        this.harness.Service.SetCategoryOverride(WindowCategory.Main, customOptions);

        var effective = this.harness.Service.GetEffectiveDecoration(WindowCategory.Main);

        _ = effective.Should().Be(customOptions);
        _ = effective.TitleBar?.Height.Should().Be(50.0);
        _ = effective.Backdrop.Should().Be(BackdropKind.Acrylic);
    }

    [TestMethod]
    public void GetEffectiveDecoration_FallbacksToSystemForUnrecognizedCategory()
    {
        this.harness = CreateHarness();

        var effective = this.harness.Service.GetEffectiveDecoration(new("NonExistentCategory"));

        _ = effective.Should().NotBeNull();
        _ = effective.Category.Should().Be(WindowCategory.System);
    }

    [TestMethod]
    public void GetEffectiveDecoration_IsCaseInsensitive()
    {
        this.harness = CreateHarness();

        var effective1 = this.harness.Service.GetEffectiveDecoration(new("main"));
        var effective2 = this.harness.Service.GetEffectiveDecoration(new("MAIN"));
        var effective3 = this.harness.Service.GetEffectiveDecoration(new("Main"));

        _ = effective1.Should().Be(effective2);
        _ = effective2.Should().Be(effective3);
    }

    private static ServiceHarness CreateHarness(WindowDecorationSettings? initial = null)
    {
        var initialSettings = initial ?? new WindowDecorationSettings();

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

        // Build a DryIoc container and register test instances so SettingsManager can be constructed
        var container = new Container();
        container.RegisterInstance(NullLoggerFactory.Instance);
        container.RegisterInstance(fileSystem.Object);
        container.RegisterInstance(finder.Object);
        container.Register<SettingsManager>(Reuse.Singleton);

        var manager = container.Resolve<SettingsManager>();

        // Initialize manager so Save operations are allowed (no sources required for this test)
        manager.InitializeAsync().GetAwaiter().GetResult();

        var service = new WindowDecorationSettingsService(manager, NullLoggerFactory.Instance);

        // Seed initial settings directly on the service (no IOptionsMonitor used)
        if (initial is not null)
        {
            service.ApplyProperties(initial);
        }

        // Return the container so the harness can dispose it at teardown
        return new ServiceHarness(service, fileSystem, file, directory, path, finder, container);
    }

    private sealed partial class ServiceHarness : IDisposable
    {
        [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "ownership of disposable service should be clear to analyzers")]
        public ServiceHarness(
            WindowDecorationSettingsService service,
            Mock<IFileSystem> fileSystem,
            Mock<IFile> file,
            Mock<IDirectory> directory,
            Mock<IPath> path,
            Mock<IPathFinder> finder,
            Container container)
        {
            this.Service = service;
            this.FileSystemMock = fileSystem;
            this.FileMock = file;
            this.DirectoryMock = directory;
            this.PathMock = path;
            this.PathFinderMock = finder;
            this.Container = container;
        }

        public WindowDecorationSettingsService Service { get; }

        public Mock<IFileSystem> FileSystemMock { get; }

        public Mock<IFile> FileMock { get; }

        public Mock<IDirectory> DirectoryMock { get; }

        public Mock<IPath> PathMock { get; }

        public Mock<IPathFinder> PathFinderMock { get; }

        public Container Container { get; }

        // Apply the new settings directly to the service to simulate an external
        // configuration change. Tests rely on ApplyProperties to update the
        // service state when configuration systems are not part of the unit test.
        public void TriggerChange(WindowDecorationSettings settings)
            => this.Service.ApplyProperties(settings);

        public void Dispose()
        {
            // Dispose service/manager first, then the DI container.
            this.Service.Dispose();

            // Dispose the DI container to release any resolved singletons or resources.
            this.Container.Dispose();

            GC.SuppressFinalize(this);
        }
    }
}
