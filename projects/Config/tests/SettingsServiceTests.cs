// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config.Tests;

using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.IO.Abstractions;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Setting Service")]
public class SettingsServiceTests
{
    private readonly Mock<IFileSystem> mockFileSystem;
    private readonly Mock<IOptionsMonitor<TestSettings>> mockSettingsMonitor;
    private readonly Mock<ILoggerFactory> mockLoggerFactory;
    private readonly Mock<ILogger<SettingsService<TestSettings>>> mockLogger;

    public SettingsServiceTests()
    {
        this.mockFileSystem = new Mock<IFileSystem>();
        this.mockSettingsMonitor = new Mock<IOptionsMonitor<TestSettings>>();
        this.mockLoggerFactory = new Mock<ILoggerFactory>();
        this.mockLogger = new Mock<ILogger<SettingsService<TestSettings>>>();
        this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Create an instance of the logger manually without using the extension method
        this.mockLoggerFactory.Setup(lf => lf.CreateLogger(It.IsAny<string>())).Returns(this.mockLogger.Object);

        // Setup the settings monitor to return default settings
        this.mockSettingsMonitor.Setup(sm => sm.CurrentValue).Returns(new TestSettings());
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
        service.Should().NotBeNull();
    }

    [TestMethod]
    public void CanDisposeProperly_WhenNotDirty()
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
    public void ShouldNotSaveSettings_WhenNotDirty()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);

        // Act
        var result = service.SaveSettings();

        // Assert
        result.Should().BeTrue();
        this.mockFileSystem.Verify(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()), Times.Never);
    }

    [TestMethod]
    public void ShouldSaveSettings_WhenDirty()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object)
        {
            FooString = "UpdatedFoo",
            BarNumber = 999,
        };
        this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: false);
        this.mockFileSystem.Setup(fs => fs.Directory.CreateDirectory(It.IsAny<string>()))
            .Returns(new Mock<IDirectoryInfo>().Object);
        this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        // Act
        var result = service.SaveSettings();

        // Assert
        result.Should().BeTrue();
        this.mockFileSystem.Verify(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()), Times.Once);
    }

    [TestMethod]
    public void LogsWarning_WhenDisposedWhileDirty()
    {
        // Arrange
        var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object)
        {
            FooString = "UpdatedFoo",
        };
        this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: false);
        this.mockFileSystem.Setup(fs => fs.Directory.CreateDirectory(It.IsAny<string>()))
            .Returns(new Mock<IDirectoryInfo>().Object);
        this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

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
    public void ShouldLogError_WhenSaveSettingsFails()
    {
        // Arrange
        using var service = new TestSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object)
        {
            FooString = "UpdatedFoo",
        };
        this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>())).Returns("testDirectory");
        this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: false);
        this.mockFileSystem.Setup(fs => fs.Directory.CreateDirectory(It.IsAny<string>()))
            .Returns(new Mock<IDirectoryInfo>().Object);
        this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()))
            .Throws(new IOException("Test Exception"));

        // Act
        var result = service.SaveSettings();

        // Assert
        result.Should().BeFalse();
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
    public void ShouldCallUpdateProperties_WhenMonitoredOptionsChange()
    {
        // Arrange
        var newSettings = new TestSettings
        {
            FooString = "NewFoo",
            BarNumber = 99,
        };
        var settingsMonitor = this.mockSettingsMonitor.Object;

        Action<TestSettings, string?>? onChangeCallback = null;

        // Setup the OnChange method to capture the callback
        this.mockSettingsMonitor.Setup(sm => sm.OnChange(It.IsAny<Action<TestSettings, string?>>()))
            .Callback<Action<TestSettings, string?>>((callback) => onChangeCallback = callback)
            .Returns(Mock.Of<IDisposable>());
        using var service = new TestSettingsService(
            settingsMonitor,
            this.mockFileSystem.Object,
            this.mockLoggerFactory.Object);

        // Act
        onChangeCallback?.Invoke(newSettings, arg2: null);
        Thread.Sleep(600); // more than the 500ms that the service uses for throttling

        // Assert
        service.FooString.Should().Be("NewFoo");
        service.BarNumber.Should().Be(99);
    }
}

public class TestSettings
{
    public string FooString { get; init; } = "DefaultFoo";

    public int BarNumber { get; init; } = 42;
}

public class TestSettingsService(
    IOptionsMonitor<TestSettings> settingsMonitor,
    IFileSystem fs,
    ILoggerFactory? loggerFactory = null)
    : SettingsService<TestSettings>(settingsMonitor, fs, loggerFactory)
{
    private string fooString = "InitialFoo";
    private int barNumber = 1;

    public string FooString
    {
        get => this.fooString;
        set => this.SetField(ref this.fooString, value);
    }

    public int BarNumber
    {
        get => this.barNumber;
        set => this.SetField(ref this.barNumber, value);
    }

    protected override void UpdateProperties(TestSettings newSettings)
    {
        this.FooString = newSettings.FooString;
        this.BarNumber = newSettings.BarNumber;
    }

    protected override string GetConfigFilePath() => "testConfig.json";

    protected override string GetConfigSectionName() => "TestSettings";

    protected override TestSettings GetSettingsSnapshot() =>
        new()
        {
            FooString = this.FooString,
            BarNumber = this.BarNumber,
        };
}
