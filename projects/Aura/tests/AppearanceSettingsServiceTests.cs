// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Tests;

/// <summary>
/// Unit tests for the <see cref="AppearanceSettingsService"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Appearance Settings")]
public class AppearanceSettingsServiceTests
{
    private Mock<IOptionsMonitor<AppearanceSettings>> mockSettingsMonitor = null!;
    private Mock<IFileSystem> mockFileSystem = null!;
    private Mock<IPathFinder> mockPathFinder = null!;
    private Mock<ILoggerFactory> mockLoggerFactory = null!;
    private Mock<ILogger<SettingsService<AppearanceSettings>>> mockLogger = null!;

    [TestInitialize]
    public void TestInitialize()
    {
        this.mockSettingsMonitor = new Mock<IOptionsMonitor<AppearanceSettings>>();
        this.mockFileSystem = new Mock<IFileSystem>();
        this.mockPathFinder = new Mock<IPathFinder>();
        this.mockLoggerFactory = new Mock<ILoggerFactory>();
        this.mockLogger = new Mock<ILogger<SettingsService<AppearanceSettings>>>();

        _ = this.mockLogger.Setup(x => x.IsEnabled(It.IsAny<LogLevel>())).Returns(value: true);
        _ = this.mockLoggerFactory.Setup(lf => lf.CreateLogger(It.IsAny<string>())).Returns(this.mockLogger.Object);

        // Setup default settings
        var defaultSettings = new AppearanceSettings
        {
            AppThemeMode = ElementTheme.Default,
            AppThemeBackgroundColor = "#00000000",
            AppThemeFontFamily = "Segoe UI Variable",
        };
        _ = this.mockSettingsMonitor.Setup(sm => sm.CurrentValue).Returns(defaultSettings);

        // Setup path finder to return a valid config file path
        _ = this.mockPathFinder.Setup(pf => pf.GetConfigFilePath(AppearanceSettings.ConfigFileName))
            .Returns("C:\\Users\\TestUser\\AppData\\Local\\TestApp\\LocalSettings.json");
    }

    [TestMethod]
    public void Constructor_CreatesInstanceSuccessfully()
    {
        // Arrange & Act
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Assert
        _ = service.Should().NotBeNull();
        _ = service.AppThemeMode.Should().Be(ElementTheme.Default);
        _ = service.AppThemeBackgroundColor.Should().Be("#00000000");
        _ = service.AppThemeFontFamily.Should().Be("Segoe UI Variable");
    }

    [TestMethod]
    public void Constructor_InitializesPropertiesFromMonitor()
    {
        // Arrange
        var customSettings = new AppearanceSettings
        {
            AppThemeMode = ElementTheme.Dark,
            AppThemeBackgroundColor = "#FF123456",
            AppThemeFontFamily = "Arial",
        };
        _ = this.mockSettingsMonitor.Setup(sm => sm.CurrentValue).Returns(customSettings);

        // Act
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Assert
        _ = service.AppThemeMode.Should().Be(ElementTheme.Dark);
        _ = service.AppThemeBackgroundColor.Should().Be("#FF123456");
        _ = service.AppThemeFontFamily.Should().Be("Arial");
    }

    [TestMethod]
    public void Constructor_CallsPathFinderToGetConfigFilePath()
    {
        // Arrange & Act
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Assert
        this.mockPathFinder.Verify(
            pf => pf.GetConfigFilePath(AppearanceSettings.ConfigFileName),
            Times.Once,
            "PathFinder should be called to get the config file path");
    }

    [TestMethod]
    public void AppThemeMode_SetValue_UpdatesProperty()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Act
        service.AppThemeMode = ElementTheme.Light;

        // Assert
        _ = service.AppThemeMode.Should().Be(ElementTheme.Light);
    }

    [TestMethod]
    public void AppThemeMode_SetValue_RaisesPropertyChangedEvent()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);
        var propertyChangedRaised = false;
        service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(service.AppThemeMode), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        service.AppThemeMode = ElementTheme.Dark;

        // Assert
        _ = propertyChangedRaised.Should().BeTrue("PropertyChanged event should be raised");
    }

    [TestMethod]
    public void AppThemeMode_SetSameValue_DoesNotRaisePropertyChangedEvent()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);
        service.AppThemeMode = ElementTheme.Light;
        var propertyChangedRaised = false;
        service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(service.AppThemeMode), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        service.AppThemeMode = ElementTheme.Light;

        // Assert
        _ = propertyChangedRaised.Should().BeFalse("PropertyChanged event should not be raised when value doesn't change");
    }

    [TestMethod]
    public void AppThemeMode_SetValue_MarksDirty()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Act
        service.AppThemeMode = ElementTheme.Dark;

        // Assert
        _ = service.IsDirty.Should().BeTrue("Service should be marked as dirty after property change");
    }

    [TestMethod]
    [DataRow(ElementTheme.Default, DisplayName = "Default Theme")]
    [DataRow(ElementTheme.Light, DisplayName = "Light Theme")]
    [DataRow(ElementTheme.Dark, DisplayName = "Dark Theme")]
    public void AppThemeMode_SetValidValues_StoresCorrectly(ElementTheme theme)
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Act
        service.AppThemeMode = theme;

        // Assert
        _ = service.AppThemeMode.Should().Be(theme);
    }

    [TestMethod]
    public void AppThemeBackgroundColor_SetValue_UpdatesProperty()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Act
        service.AppThemeBackgroundColor = "#FFFFFFFF";

        // Assert
        _ = service.AppThemeBackgroundColor.Should().Be("#FFFFFFFF");
    }

    [TestMethod]
    public void AppThemeBackgroundColor_SetValue_RaisesPropertyChangedEvent()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);
        var propertyChangedRaised = false;
        service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(service.AppThemeBackgroundColor), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        service.AppThemeBackgroundColor = "#FF123456";

        // Assert
        _ = propertyChangedRaised.Should().BeTrue("PropertyChanged event should be raised");
    }

    [TestMethod]
    public void AppThemeBackgroundColor_SetSameValue_DoesNotRaisePropertyChangedEvent()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);
        const string testColor = "#FF123456";
        service.AppThemeBackgroundColor = testColor;
        var propertyChangedRaised = false;
        service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(service.AppThemeBackgroundColor), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        service.AppThemeBackgroundColor = testColor;

        // Assert
        _ = propertyChangedRaised.Should().BeFalse("PropertyChanged event should not be raised when value doesn't change");
    }

    [TestMethod]
    [DataRow("#FFFFFF", DisplayName = "6-digit hex color")]
    [DataRow("#FF000000", DisplayName = "8-digit hex color with alpha")]
    [DataRow("#12ABCD", DisplayName = "6-digit hex with mixed case")]
    [DataRow("#AABBCCDD", DisplayName = "8-digit hex with mixed case")]
    public void AppThemeBackgroundColor_SetValidHexColor_StoresCorrectly(string color)
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Act
        service.AppThemeBackgroundColor = color;

        // Assert
        _ = service.AppThemeBackgroundColor.Should().Be(color);
    }

    [TestMethod]
    public void AppThemeFontFamily_SetValue_UpdatesProperty()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Act
        service.AppThemeFontFamily = "Arial";

        // Assert
        _ = service.AppThemeFontFamily.Should().Be("Arial");
    }

    [TestMethod]
    public void AppThemeFontFamily_SetValue_RaisesPropertyChangedEvent()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);
        var propertyChangedRaised = false;
        service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(service.AppThemeFontFamily), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        service.AppThemeFontFamily = "Times New Roman";

        // Assert
        _ = propertyChangedRaised.Should().BeTrue("PropertyChanged event should be raised");
    }

    [TestMethod]
    [DataRow("Arial", DisplayName = "Arial font")]
    [DataRow("Courier New", DisplayName = "Courier New font")]
    [DataRow("Times New Roman", DisplayName = "Times New Roman font")]
    [DataRow("Segoe UI", DisplayName = "Segoe UI font")]
    public void AppThemeFontFamily_SetValidFontNames_StoresCorrectly(string fontFamily)
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Act
        service.AppThemeFontFamily = fontFamily;

        // Assert
        _ = service.AppThemeFontFamily.Should().Be(fontFamily);
    }

    [TestMethod]
    public void UpdateProperties_WhenSettingsChange_UpdatesAllProperties()
    {
        // Arrange
        var newSettings = new AppearanceSettings
        {
            AppThemeMode = ElementTheme.Dark,
            AppThemeBackgroundColor = "#FF112233",
            AppThemeFontFamily = "Consolas",
        };

        Action<AppearanceSettings, string?>? onChangeCallback = null;
        _ = this.mockSettingsMonitor.Setup(sm => sm.OnChange(It.IsAny<Action<AppearanceSettings, string?>>()))
            .Callback<Action<AppearanceSettings, string?>>(callback => onChangeCallback = callback)
            .Returns(Mock.Of<IDisposable>());

        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Act
        onChangeCallback?.Invoke(newSettings, null);
        Thread.Sleep(600); // Wait for throttle (500ms)

        // Assert
        _ = service.AppThemeMode.Should().Be(ElementTheme.Dark);
        _ = service.AppThemeBackgroundColor.Should().Be("#FF112233");
        _ = service.AppThemeFontFamily.Should().Be("Consolas");
    }

    [TestMethod]
    public void UpdateProperties_WhenSettingsChange_RaisesPropertyChangedEvents()
    {
        // Arrange
        var newSettings = new AppearanceSettings
        {
            AppThemeMode = ElementTheme.Dark,
            AppThemeBackgroundColor = "#FF112233",
            AppThemeFontFamily = "Consolas",
        };

        Action<AppearanceSettings, string?>? onChangeCallback = null;
        _ = this.mockSettingsMonitor.Setup(sm => sm.OnChange(It.IsAny<Action<AppearanceSettings, string?>>()))
            .Callback<Action<AppearanceSettings, string?>>(callback => onChangeCallback = callback)
            .Returns(Mock.Of<IDisposable>());

        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        var changedProperties = new List<string>();
        service.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName != null)
            {
                changedProperties.Add(args.PropertyName);
            }
        };

        // Act
        onChangeCallback?.Invoke(newSettings, null);
        Thread.Sleep(600); // Wait for throttle (500ms)

        // Assert
        _ = changedProperties.Should().Contain(nameof(service.AppThemeMode));
        _ = changedProperties.Should().Contain(nameof(service.AppThemeBackgroundColor));
        _ = changedProperties.Should().Contain(nameof(service.AppThemeFontFamily));
    }

    [TestMethod]
    public void SaveSettings_WhenNotDirty_ReturnsTrue()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Act
        var result = service.SaveSettings();

        // Assert
        _ = result.Should().BeTrue("SaveSettings should return true when not dirty");
        this.mockFileSystem.Verify(
            fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()),
            Times.Never,
            "File should not be written when settings are not dirty");
    }

    [TestMethod]
    public void SaveSettings_WhenDirty_WritesToFile()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        service.AppThemeMode = ElementTheme.Dark;

        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>()))
            .Returns("C:\\Users\\TestUser\\AppData\\Local\\TestApp");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: true);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        // Act
        var result = service.SaveSettings();

        // Assert
        _ = result.Should().BeTrue("SaveSettings should return true when successful");
        this.mockFileSystem.Verify(
            fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()),
            Times.Once,
            "File should be written when settings are dirty");
    }

    [TestMethod]
    public void SaveSettings_WhenDirty_ClearsDirtyFlag()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        service.AppThemeMode = ElementTheme.Dark;

        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>()))
            .Returns("C:\\Users\\TestUser\\AppData\\Local\\TestApp");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: true);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        // Act
        _ = service.SaveSettings();

        // Assert
        _ = service.IsDirty.Should().BeFalse("IsDirty should be false after successful save");
    }

    [TestMethod]
    public void SaveSettings_WhenExceptionOccurs_ReturnsFalse()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        service.AppThemeMode = ElementTheme.Dark;

        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>()))
            .Returns("C:\\Users\\TestUser\\AppData\\Local\\TestApp");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: true);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()))
            .Throws(new IOException("Test Exception"));

        // Act
        var result = service.SaveSettings();

        // Assert
        _ = result.Should().BeFalse("SaveSettings should return false when an exception occurs");
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once,
            "Error should be logged when save fails");
    }

    [TestMethod]
    public void Dispose_WhenNotDirty_DisposesWithoutWarning()
    {
        // Arrange
        var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        // Act
        service.Dispose();
        service.Dispose(); // Call dispose again to ensure no exceptions

        // Assert
        this.mockLogger.Verify(
            x => x.Log(
                It.IsIn(LogLevel.Error, LogLevel.Warning),
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Never,
            "No errors or warnings should be logged when disposed while not dirty");
    }

    [TestMethod]
    public void Dispose_WhenDirty_LogsWarning()
    {
        // Arrange
        var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object)
        {
            AppThemeMode = ElementTheme.Dark,
        };

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
            Times.Once,
            "Warning should be logged when disposed while dirty");
    }

    [TestMethod]
    public void IntegrationTest_MultiplePropertyChanges_MaintainsDirtyStateCorrectly()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        _ = this.mockFileSystem.Setup(fs => fs.Path.GetDirectoryName(It.IsAny<string>()))
            .Returns("C:\\Users\\TestUser\\AppData\\Local\\TestApp");
        _ = this.mockFileSystem.Setup(fs => fs.Directory.Exists(It.IsAny<string>())).Returns(value: true);
        _ = this.mockFileSystem.Setup(fs => fs.File.WriteAllText(It.IsAny<string>(), It.IsAny<string>()));

        // Act & Assert
        _ = service.IsDirty.Should().BeFalse("Service should not be dirty initially");

        service.AppThemeMode = ElementTheme.Dark;
        _ = service.IsDirty.Should().BeTrue("Service should be dirty after first change");

        service.AppThemeBackgroundColor = "#FF123456";
        _ = service.IsDirty.Should().BeTrue("Service should remain dirty after second change");

        service.AppThemeFontFamily = "Arial";
        _ = service.IsDirty.Should().BeTrue("Service should remain dirty after third change");

        _ = service.SaveSettings();
        _ = service.IsDirty.Should().BeFalse("Service should not be dirty after save");
    }

    [TestMethod]
    public void IntegrationTest_PropertyChangeEventSequence_FiresCorrectly()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.mockSettingsMonitor.Object,
            this.mockFileSystem.Object,
            this.mockPathFinder.Object,
            this.mockLoggerFactory.Object);

        var eventSequence = new List<string>();
        service.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName is not null && !string.Equals(args.PropertyName, nameof(service.IsDirty), StringComparison.Ordinal))
            {
                eventSequence.Add(args.PropertyName);
            }
        };

        // Act
        service.AppThemeMode = ElementTheme.Dark;
        service.AppThemeBackgroundColor = "#FF123456";
        service.AppThemeFontFamily = "Arial";

        // Assert
        _ = eventSequence.Should().HaveCount(3, "Three property changes should fire three events");
        _ = eventSequence[0].Should().Be(nameof(service.AppThemeMode));
        _ = eventSequence[1].Should().Be(nameof(service.AppThemeBackgroundColor));
        _ = eventSequence[2].Should().Be(nameof(service.AppThemeFontFamily));
    }
}
