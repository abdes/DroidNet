// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Config.Sources;
using DroidNet.Config.Tests.TestHelpers;
using DryIoc;
using FluentAssertions;

namespace DroidNet.Config.Tests.Integration;

/// <summary>
/// Integration tests for DI container setup and bootstrapper validation.
/// Tests service resolution, registration, and full DI pipeline with BootstrapperExtensions.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DI Integration")]
public class DependencyInjectionTests : SettingsTestBase
{
    /// <summary>
    /// Disable default settings service registrations since these tests explicitly test
    /// the WithSettingsService bootstrapper extension which registers services itself.
    /// </summary>
    protected override bool RegisterDefaultSettingsServices => false;

    [TestMethod]
    public void WithSettings_ShouldRegisterSettingsManager()
    {
        // Arrange
        var settings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings() }
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);

        // Act
        this.Container.WithSettings(new[] { filePath });

        // Assert
        var manager = this.Container.Resolve<ISettingsManager>();
        _ = manager.Should().NotBeNull();
        _ = manager.Should().BeOfType<SettingsManager>();
    }

    [TestMethod]
    public void WithSettings_ShouldRegisterSettingsSources()
    {
        // Arrange
        var settings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings() }
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);

        // Act
        this.Container.WithSettings(new[] { filePath });

        // Assert
        var sources = this.Container.Resolve<IEnumerable<ISettingsSource>>();
        _ = sources.Should().NotBeNull();
        _ = sources.Should().HaveCount(1);
        _ = sources.First().Should().BeOfType<JsonSettingsSource>();
    }

    [TestMethod]
    public void WithSettingsService_ShouldRegisterTypedService()
    {
        // Arrange
        var settings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings() }
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);

        // Act
        this.Container
            .WithSettings(new[] { filePath })
            .WithSettingsService<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        manager.InitializeAsync().Wait();

        // Assert
        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        _ = service.Should().NotBeNull();
    }

    [TestMethod]
    public void WithSettingsService_ShouldReturnSingletonInstance()
    {
        // Arrange
        var settings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings() }
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);

        this.Container
            .WithSettings(new[] { filePath })
            .WithSettingsService<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        manager.InitializeAsync().Wait();

        // Act
        var service1 = this.Container.Resolve<ISettingsService<ITestSettings>>();
        var service2 = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Assert
        _ = service1.Should().BeSameAs(service2);
    }

    [TestMethod]
    public void WithSettings_WithMultipleFiles_ShouldRegisterAllSources()
    {
        // Arrange
        var settings1 = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings { Name = "File1" } }
        };
        var settings2 = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings { Name = "File2" } }
        };

        var file1 = this.CreateMultiSectionSettingsFile("test1.json", settings1);
        var file2 = this.CreateMultiSectionSettingsFile("test2.json", settings2);

        // Act
        this.Container.WithSettings(new[] { file1, file2 });

        // Assert
        var sources = this.Container.Resolve<IEnumerable<ISettingsSource>>();
        _ = sources.Should().HaveCount(2);
    }

    [TestMethod]
    public async Task WithSettings_ShouldSupportLastLoadedWinsStrategy()
    {
        // Arrange
        var settings1 = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings { Name = "First", Value = 1 } }
        };
        var settings2 = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings { Name = "Second", Value = 2 } }
        };

        var file1 = this.CreateMultiSectionSettingsFile("first.json", settings1);
        var file2 = this.CreateMultiSectionSettingsFile("second.json", settings2);

        this.Container
            .WithSettings(new[] { file1, file2 })
            .WithSettingsService<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync();

        // Act
        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        await service.InitializeAsync();

        // Assert
        _ = service.Settings.Name.Should().Be("Second");
        _ = service.Settings.Value.Should().Be(2);
    }

    [TestMethod]
    public void WithSettings_WithMultipleSettingsTypes_ShouldResolveBoth()
    {
        // Arrange
        var settings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings() },
            { nameof(AlternativeTestSettings), new AlternativeTestSettings() }
        };
        var filePath = this.CreateMultiSectionSettingsFile("multi.json", settings);

        this.Container
            .WithSettings(new[] { filePath })
            .WithSettingsService<ITestSettings, TestSettingsService>()
            .WithSettingsService<IAlternativeTestSettings, AlternativeTestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        manager.InitializeAsync().Wait();

        // Act
        var service1 = this.Container.Resolve<ISettingsService<ITestSettings>>();
        var service2 = this.Container.Resolve<ISettingsService<IAlternativeTestSettings>>();

        // Assert
        _ = service1.Should().NotBeNull();
        _ = service2.Should().NotBeNull();
        _ = service1.Should().NotBeSameAs(service2);
    }

    [TestMethod]
    public void WithSettings_WithEmptyFileList_ShouldThrowArgumentException()
    {
        // Act
        var act = () => this.Container.WithSettings(Array.Empty<string>());

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithMessage("*at least one*");
    }

    [TestMethod]
    public void WithSettings_WithNullContainer_ShouldThrowArgumentNullException()
    {
        // Arrange
        IContainer? nullContainer = null;

        // Act
        var act = () => nullContainer!.WithSettings(new[] { "test.json" });

        // Assert
        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void WithSettingsService_WithNullContainer_ShouldThrowArgumentNullException()
    {
        // Arrange
        IContainer? nullContainer = null;

        // Act
        var act = () => nullContainer!.WithSettingsService<ITestSettings, TestSettingsService>();

        // Assert
        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void WithSettings_WithUnsupportedExtension_ShouldThrowArgumentException()
    {
        // Arrange
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "test.xml");

        // Act
        var act = () => this.Container.WithSettings(new[] { filePath });

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithMessage("*Unrecognized settings file extension*");
    }

    [TestMethod]
    public void WithSettings_WithSecureJsonExtension_ShouldThrowNotSupportedException()
    {
        // Arrange
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "test.secure.json");

        // Act
        var act = () => this.Container.WithSettings(new[] { filePath });

        // Assert
        _ = act.Should().Throw<NotSupportedException>()
            .WithMessage("*not yet implemented*");
    }

    [TestMethod]
    public async Task FullPipeline_WithRealScenario_ShouldWorkEndToEnd()
    {
        // Arrange
        var appSettings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings { Name = "AppSettings", Value = 100 } }
        };
        var userSettings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings { Name = "UserSettings", Value = 200 } }
        };

        var appFile = this.CreateMultiSectionSettingsFile("appsettings.json", appSettings);
        var userFile = this.CreateMultiSectionSettingsFile("user-settings.json", userSettings);

        // Act - Setup container like a real application would
        this.Container
            .WithSettings(new[] { appFile, userFile })
            .WithSettingsService<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync();

        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        await service.InitializeAsync();

        // Modify and save
        service.Settings.Value = 999;

        Console.WriteLine($"[DEBUG TEST] Service type: {service.GetType().FullName}");
        Console.WriteLine($"[DEBUG TEST] Service base type: {service.GetType().BaseType?.FullName}");

        var isDirtyProperty = service.GetType().GetProperty("IsDirty");
        Console.WriteLine($"[DEBUG TEST] IsDirty property: {isDirtyProperty?.Name ?? "NULL"}");
        Console.WriteLine($"[DEBUG TEST] IsDirty property type: {isDirtyProperty?.PropertyType.FullName ?? "NULL"}");
        Console.WriteLine($"[DEBUG TEST] IsDirty can read: {isDirtyProperty?.CanRead}");
        Console.WriteLine($"[DEBUG TEST] IsDirty can write: {isDirtyProperty?.CanWrite}");
        Console.WriteLine($"[DEBUG TEST] IsDirty declaring type: {isDirtyProperty?.DeclaringType?.FullName ?? "NULL"}");
        Console.WriteLine($"[DEBUG TEST] IsDirty reflected type: {isDirtyProperty?.ReflectedType?.FullName ?? "NULL"}");
        Console.WriteLine($"[DEBUG TEST] IsDirty get method: {isDirtyProperty?.GetMethod?.Name ?? "NULL"}");
        Console.WriteLine($"[DEBUG TEST] IsDirty set method: {isDirtyProperty?.SetMethod?.Name ?? "NULL"}");

        if (isDirtyProperty != null)
        {
            Console.WriteLine($"[DEBUG TEST] Attempting to set IsDirty to true on object of type {service.GetType().FullName}");
        }

        isDirtyProperty?.SetValue(service, true);
        await service.SaveAsync();

        // Assert
        _ = service.Settings.Name.Should().Be("UserSettings");
        _ = service.Settings.Value.Should().Be(999);
        _ = service.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public void WithSettings_ShouldRegisterFileSystemDependency()
    {
        // Arrange
        var settings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings() }
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);

        // Act
        this.Container.WithSettings(new[] { filePath });

        // Assert
        var fileSystem = this.Container.Resolve<System.IO.Abstractions.IFileSystem>();
        _ = fileSystem.Should().NotBeNull();
    }

    [TestMethod]
    public void WithSettings_ShouldRegisterLoggerFactoryDependency()
    {
        // Arrange
        var settings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings() }
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);

        // Act
        this.Container.WithSettings(new[] { filePath });

        // Assert
        var loggerFactory = this.Container.Resolve<Microsoft.Extensions.Logging.ILoggerFactory>();
        _ = loggerFactory.Should().NotBeNull();
    }

    [TestMethod]
    public async Task WithSettings_AfterServiceResolution_ShouldProvideInitializedData()
    {
        // Arrange
        var settings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings { Name = "InitialData", Value = 42 } }
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);

        this.Container
            .WithSettings(new[] { filePath })
            .WithSettingsService<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync();

        // Act
        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        await service.InitializeAsync();

        // Assert
        _ = service.Settings.Name.Should().Be("InitialData");
        _ = service.Settings.Value.Should().Be(42);
    }

    [TestMethod]
    public void SettingsManager_ShouldBeSingleton()
    {
        // Arrange
        var settings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings() }
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);

        this.Container.WithSettings(new[] { filePath });

        // Act
        var manager1 = this.Container.Resolve<ISettingsManager>();
        var manager2 = this.Container.Resolve<ISettingsManager>();

        // Assert
        _ = manager1.Should().BeSameAs(manager2);
    }

    [TestMethod]
    public async Task WithSettings_WithValidation_ShouldEnforceConstraints()
    {
        // Arrange
        var settings = new Dictionary<string, object>
        {
            { nameof(TestSettings), new TestSettings { Name = "Test", Value = 50 } }
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);

        this.Container
            .WithSettings(new[] { filePath })
            .WithSettingsService<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync();

        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        await service.InitializeAsync();

        // Act - Set invalid value
        service.Settings.Name = string.Empty; // Violates StringLength minimum

        Console.WriteLine($"[DEBUG TEST] Service type: {service.GetType().FullName}");
        Console.WriteLine($"[DEBUG TEST] Service base type: {service.GetType().BaseType?.FullName}");

        var isDirtyProperty = service.GetType().GetProperty("IsDirty");
        Console.WriteLine($"[DEBUG TEST] IsDirty property: {isDirtyProperty?.Name ?? "NULL"}");
        Console.WriteLine($"[DEBUG TEST] IsDirty property type: {isDirtyProperty?.PropertyType.FullName ?? "NULL"}");
        Console.WriteLine($"[DEBUG TEST] IsDirty can read: {isDirtyProperty?.CanRead}");
        Console.WriteLine($"[DEBUG TEST] IsDirty can write: {isDirtyProperty?.CanWrite}");
        Console.WriteLine($"[DEBUG TEST] IsDirty declaring type: {isDirtyProperty?.DeclaringType?.FullName ?? "NULL"}");
        Console.WriteLine($"[DEBUG TEST] IsDirty set method: {isDirtyProperty?.SetMethod?.Name ?? "NULL"}");

        isDirtyProperty?.SetValue(service, true);

        var act = async () => await service.SaveAsync();

        // Assert
        _ = await act.Should().ThrowAsync<SettingsValidationException>();
    }
}
