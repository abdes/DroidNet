// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Moq;

namespace DroidNet.Config.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Setting Service")]
public class SettingsServiceTests
{
    private readonly Mock<IFileSystem> mockFileSystem;
    private readonly Mock<IOptionsMonitor<ITestSettings>> mockSettingsMonitor;
    private readonly Mock<ILoggerFactory> mockLoggerFactory;
    private readonly Mock<ILogger<SettingsService<ITestSettings>>> mockLogger;

    public SettingsServiceTests()
    {
        this.mockFileSystem = new Mock<IFileSystem>();
        this.mockSettingsMonitor = new Mock<IOptionsMonitor<ITestSettings>>();
        this.mockLoggerFactory = new Mock<ILoggerFactory>();
        this.mockLogger = new Mock<ILogger<SettingsService<ITestSettings>>>();
        _ = this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Create an instance of the logger manually without using the extension method
        _ = this.mockLoggerFactory.Setup(lf => lf.CreateLogger(It.IsAny<string>())).Returns(this.mockLogger.Object);

        // Setup the settings monitor to return default settings - we don't need to return anything specific
        // as the service implementation handles the initial values
    }

    [TestMethod]
    public void CanCreate()
    {
        // Arrange & Act
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);

        // Assert
        _ = service.Should().NotBeNull();
    }

    [TestMethod]
    public void CanDisposeProperlyWhenNotDirty()
    {
        // Arrange
        var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);

        // Act
        service.Dispose();
        service.Dispose(); // Call dispose again to ensure no exceptions are thrown

        // Assert
        this.mockLogger.Verify(
            x => x.Log(
                It.IsIn(LogLevel.Error, LogLevel.Warning),
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Never);
    }

    [TestMethod]
    public void ShouldNotSaveSettingsWhenNotDirty()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);

        // Act
        var result = service.SaveSettings();

        // Assert
        _ = result.Should().BeTrue();
        this.mockFileSystem.Verify(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()), Times.Never);
    }

    [TestMethod]
    public void ShouldSaveSettingsWhenDirty()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);
        service.FooString = "UpdatedFoo";
        service.BarNumber = 999;
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: false);
        _ = this.mockFileSystem.Setup(fs => fs.Directory.CreateDirectory(It.IsAny<string>()))
            .Returns(new Mock<IDirectoryInfo>().Object);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        // Act
        var result = service.SaveSettings();

        // Assert
        _ = result.Should().BeTrue();
        this.mockFileSystem.Verify(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()), Times.Once);
    }

    [TestMethod]
    public void LogsWarningWhenDisposedWhileDirty()
    {
        // Arrange
        var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object)
        {
            FooString = "UpdatedFoo",
        };
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: false);
        _ = this.mockFileSystem.Setup(fs => fs.Directory.CreateDirectory(It.IsAny<string>()))
            .Returns(new Mock<IDirectoryInfo>().Object);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        // Act
        service.Dispose();

        // Assert
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Warning,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
    }

    [TestMethod]
    public void ShouldLogErrorWhenSaveSettingsFails()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);
        service.FooString = "UpdatedFoo";
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: false);
        _ = this.mockFileSystem.Setup(fs => fs.Directory.CreateDirectory(It.IsAny<string>()))
            .Returns(new Mock<IDirectoryInfo>().Object);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()))
            .Throws(new IOException("Test Exception"));

        // Act
        var result = service.SaveSettings();

        // Assert
        _ = result.Should().BeFalse();
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
    }

    [TestMethod]
    public void ShouldCallUpdatePropertiesWhenMonitoredOptionsChange()
    {
        // Arrange
        var newSettings = Mock.Of<ITestSettings>(s => s.FooString == "NewFoo" && s.BarNumber == 99);
        var settingsMonitor = this.mockSettingsMonitor.Object;

        Action<ITestSettings, string?>? onChangeCallback = null;

        // Setup the OnChange method to capture the callback
        _ = this.mockSettingsMonitor.Setup(sm => sm.OnChange(It.IsAny<Action<ITestSettings, string?>>()))
            .Callback<Action<ITestSettings, string?>>((callback) => onChangeCallback = callback)
            .Returns(Mock.Of<IDisposable>());
        using var service = new TestSettingsService(
            settingsMonitor,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);

        // Act
        onChangeCallback?.Invoke(newSettings, arg2: null);
        Thread.Sleep(600); // more than the 500ms that the service uses for throttling

        // Assert
        _ = service.FooString.Should().Be("NewFoo");
        _ = service.BarNumber.Should().Be(99);
    }

    [TestMethod]
    public void SettingsPropertyShouldReturnServiceInstance()
    {
        // Arrange & Act
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);

        // Assert
        // The Settings property returns the service instance cast to ITestSettings
        // Since TestSettingsService implements ITestSettings, this should work
        _ = service.Settings.Should().BeSameAs(service);
        _ = service.Settings.Should().BeAssignableTo<ITestSettings>();
    }

    [TestMethod]
    public void PropertyChangedShouldFireWhenPropertyIsModified()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);
        var propertyChangedEvents = new List<string>();
        service.PropertyChanged += (_, args) => propertyChangedEvents.Add(args.PropertyName ?? string.Empty);

        // Act
        service.FooString = "ChangedValue";
        service.BarNumber = 42;

        // Assert
        _ = propertyChangedEvents.Should().Contain("FooString");
        _ = propertyChangedEvents.Should().Contain("BarNumber");
    }

    [TestMethod]
    public void PropertyChangedShouldNotFireWhenPropertySetToSameValue()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);
        var initialValue = service.FooString;
        var propertyChangedFired = false;
        service.PropertyChanged += (_, _) => propertyChangedFired = true;

        // Act
        service.FooString = initialValue;

        // Assert
        _ = propertyChangedFired.Should().BeFalse();
    }

    [TestMethod]
    public void IsDirtyShouldBeTrueAfterPropertyChange()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);

        // Act
        service.FooString = "Modified";

        // Assert
        _ = service.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public void IsDirtyShouldBeFalseAfterSuccessfulSave()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);
        service.FooString = "Modified";
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: true);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        // Act
        var result = service.SaveSettings();

        // Assert
        _ = result.Should().BeTrue();
        _ = service.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public void IsDirtyShouldRemainTrueAfterFailedSave()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);
        service.FooString = "Modified";
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: true);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()))
            .Throws(new IOException("Test Exception"));

        // Act
        var result = service.SaveSettings();

        // Assert
        _ = result.Should().BeFalse();
        _ = service.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public void AutoSaveShouldSaveSettingsAfterDelay()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object,
            autoSaveDelay: TimeSpan.FromMilliseconds(100));
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: true);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        // Act
        service.FooString = "AutoSaveTest";
        Thread.Sleep(200); // Wait for auto-save to trigger

        // Assert
        this.mockFileSystem.Verify(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()), Times.Once);
    }

    [TestMethod]
    public void AutoSaveShouldNotSaveWhenDisabled()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object,
            autoSaveDelay: TimeSpan.Zero);
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: true);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        // Act
        service.FooString = "NoAutoSave";
        Thread.Sleep(200); // Wait to ensure no auto-save happens

        // Assert
        this.mockFileSystem.Verify(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()), Times.Never);
    }

    [TestMethod]
    public void DisposeShouldSaveDirtySettingsWhenAutoSaveIsEnabled()
    {
        // Arrange
        var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object,
            autoSaveDelay: TimeSpan.FromSeconds(10))
        {
            FooString = "ModifiedValue",
        };

        // Long delay to prevent auto-save during test
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: true);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        // Act
        service.Dispose();

        // Assert
        this.mockFileSystem.Verify(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()), Times.Once);
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Warning,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
    }

    [TestMethod]
    public void ShouldSaveSettingsWithoutSectionName()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object,
            useSectionName: false);
        service.FooString = "UpdatedFoo";
        service.BarNumber = 999;
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: true);
        var savedContent = string.Empty;
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()))
            .Callback<string, string>((_, content) => savedContent = content);

        // Act
        var result = service.SaveSettings();

        // Assert
        _ = result.Should().BeTrue();
        this.mockFileSystem.Verify(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()), Times.Once);
        _ = savedContent.Should().NotContain("TestSettings"); // Should not have section wrapper
        _ = savedContent.Should().Contain("UpdatedFoo");
        _ = savedContent.Should().Contain("999");
    }
}
