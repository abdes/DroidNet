// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Config.Tests.TestHelpers;
using FluentAssertions;

namespace DroidNet.Config.Tests.Validation;

/// <summary>
/// Comprehensive tests for property-level validation using data annotations.
/// Tests validation infrastructure, error reporting, and validation scenarios.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings Validation")]
public class ValidationTests : SettingsTestBase
{
    [TestMethod]
    public async Task ValidateAsync_WithAllValidProperties_ShouldReturnNoErrors()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        var validSettings = new TestSettings
        {
            Name = "ValidName",
            Value = 50,
            IsEnabled = true
        };
        mockSource.AddSection("TestSettings", validSettings);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new TestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Act
        var errors = await service.ValidateAsync();

        // Assert
        _ = errors.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ValidateAsync_WithRequiredFieldViolation_ShouldReturnError()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        var invalidSettings = new InvalidTestSettings
        {
            RequiredField = null // Required field violation
        };
        mockSource.AddSection("InvalidTestSettings", invalidSettings);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new InvalidTestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Act
        var errors = await service.ValidateAsync();

        // Assert
        _ = errors.Should().NotBeEmpty();
        _ = errors.Should().Contain(e => e.PropertyName == "RequiredField");
        _ = errors.Should().Contain(e => e.Message.Contains("required", StringComparison.OrdinalIgnoreCase));
    }

    [TestMethod]
    public async Task ValidateAsync_WithRangeViolation_ShouldReturnError()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        var invalidSettings = new InvalidTestSettings
        {
            RequiredField = "Present",
            OutOfRangeValue = 999, // Range is 1-10
            InvalidEmail = "valid@email.com"
        };
        mockSource.AddSection("InvalidTestSettings", invalidSettings);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new InvalidTestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Act
        var errors = await service.ValidateAsync();

        // Assert
        _ = errors.Should().NotBeEmpty();
        _ = errors.Should().Contain(e => e.PropertyName == "OutOfRangeValue");
        _ = errors.Should().Contain(e => e.Message.Contains("range", StringComparison.OrdinalIgnoreCase)
            || e.Message.Contains("1") || e.Message.Contains("10"));
    }

    [TestMethod]
    public async Task ValidateAsync_WithEmailAddressViolation_ShouldReturnError()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        var invalidSettings = new InvalidTestSettings
        {
            RequiredField = "Present",
            OutOfRangeValue = 5,
            InvalidEmail = "not-an-email" // Invalid email format
        };
        mockSource.AddSection("InvalidTestSettings", invalidSettings);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new InvalidTestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Act
        var errors = await service.ValidateAsync();

        // Assert
        _ = errors.Should().NotBeEmpty();
        _ = errors.Should().Contain(e => e.PropertyName == "InvalidEmail");
    }

    [TestMethod]
    public async Task ValidateAsync_WithMultipleViolations_ShouldReturnAllErrors()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        var invalidSettings = new InvalidTestSettings
        {
            RequiredField = null, // Violation 1
            OutOfRangeValue = 999, // Violation 2
            InvalidEmail = "not-an-email" // Violation 3
        };
        mockSource.AddSection("InvalidTestSettings", invalidSettings);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new InvalidTestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Act
        var errors = await service.ValidateAsync();

        // Assert
        _ = errors.Should().HaveCountGreaterThanOrEqualTo(2);
        _ = errors.Select(e => e.PropertyName).Should().Contain("RequiredField");
        _ = errors.Select(e => e.PropertyName).Should().Contain("OutOfRangeValue");
    }

    [TestMethod]
    public async Task ValidateAsync_WithStringLengthViolation_ShouldReturnError()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        var invalidSettings = new TestSettings
        {
            Name = string.Empty, // StringLength minimum is 1
            Value = 50
        };
        mockSource.AddSection("TestSettings", invalidSettings);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new TestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Act
        var errors = await service.ValidateAsync();

        // Assert
        _ = errors.Should().NotBeEmpty();
        _ = errors.Should().Contain(e => e.PropertyName == "Name");
    }

    [TestMethod]
    public async Task SaveAsync_WithValidationErrors_ShouldThrowSettingsValidationException()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        var invalidSettings = new InvalidTestSettings
        {
            RequiredField = null,
            OutOfRangeValue = 999,
            InvalidEmail = "not-an-email"
        };
        mockSource.AddSection("InvalidTestSettings", invalidSettings);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new InvalidTestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Mark as dirty to trigger save
        var isDirtyProperty = typeof(Config.SettingsService<IInvalidTestSettings>).GetProperty("IsDirty");
        isDirtyProperty?.SetValue(service, true);

        // Act
        var act = async () => await service.SaveAsync();

        // Assert
        var exception = await act.Should().ThrowAsync<SettingsValidationException>();
        _ = exception.Which.ValidationErrors.Should().NotBeEmpty();
    }

    [TestMethod]
    public void SettingsValidationError_ShouldHavePropertyNameAndMessage()
    {
        // Arrange & Act
        var error = new SettingsValidationError("TestProperty", "Test error message");

        // Assert
        _ = error.PropertyName.Should().Be("TestProperty");
        _ = error.Message.Should().Be("Test error message");
    }

    [TestMethod]
    public void SettingsValidationException_ShouldContainValidationErrors()
    {
        // Arrange
        var errors = new List<SettingsValidationError>
        {
            new("Property1", "Error 1"),
            new("Property2", "Error 2")
        };

        // Act
        var exception = new SettingsValidationException("Validation failed", errors);

        // Assert
        _ = exception.Message.Should().Be("Validation failed");
        _ = exception.ValidationErrors.Should().HaveCount(2);
        _ = exception.ValidationErrors.Should().Contain(e => e.PropertyName == "Property1");
        _ = exception.ValidationErrors.Should().Contain(e => e.PropertyName == "Property2");
    }

    [TestMethod]
    public async Task ValidateAsync_WithRangeBoundaryValues_ShouldValidateCorrectly()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");

        // Test minimum boundary
        var settingsAtMin = new TestSettings
        {
            Name = "Test",
            Value = 0 // Range is 0-1000
        };
        mockSource.AddSection("TestSettings", settingsAtMin);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new TestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Act
        var errorsMin = await service.ValidateAsync();

        // Assert
        _ = errorsMin.Should().BeEmpty();

        // Test maximum boundary
        service.Settings.Value = 1000;
        var errorsMax = await service.ValidateAsync();
        _ = errorsMax.Should().BeEmpty();

        // Test below minimum
        service.Settings.Value = -1;
        var errorsBelowMin = await service.ValidateAsync();
        _ = errorsBelowMin.Should().NotBeEmpty();

        // Test above maximum
        service.Settings.Value = 1001;
        var errorsAboveMax = await service.ValidateAsync();
        _ = errorsAboveMax.Should().NotBeEmpty();
    }

    [TestMethod]
    public async Task ValidateAsync_WithNullableProperties_ShouldHandleCorrectly()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        var settings = new TestSettings
        {
            Name = "Test",
            Value = 50,
            Description = null // Nullable property
        };
        mockSource.AddSection("TestSettings", settings);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new TestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Act
        var errors = await service.ValidateAsync();

        // Assert
        _ = errors.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ValidateAsync_ShouldValidateAllProperties()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        var settings = new TestSettings
        {
            Name = "A", // Valid - minimum length 1
            Value = 500, // Valid - within range 0-1000
            IsEnabled = true
        };
        mockSource.AddSection("TestSettings", settings);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new TestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Act
        var errors = await service.ValidateAsync();

        // Assert - All properties should pass validation
        _ = errors.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ValidateAsync_WithCancellation_ShouldComplete()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        mockSource.AddSection("TestSettings", new TestSettings());

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new TestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        var cts = new CancellationTokenSource();

        // Act - Validation doesn't actually support cancellation in current implementation
        var errors = await service.ValidateAsync(cts.Token);

        // Assert
        _ = errors.Should().NotBeNull();
    }

    [TestMethod]
    public void SettingsValidationError_ToString_ShouldFormatCorrectly()
    {
        // Arrange
        var error = new SettingsValidationError("TestProperty", "Test error message");

        // Act
        var result = error.ToString();

        // Assert
        _ = result.Should().Contain("TestProperty");
        _ = result.Should().Contain("Test error message");
    }

    [TestMethod]
    public async Task ValidateAsync_WithExtremelyLongString_ShouldReturnError()
    {
        // Arrange
        var mockSource = new MockSettingsSource("test-source");
        var settings = new TestSettings
        {
            Name = new string('A', 101), // StringLength max is 100
            Value = 50
        };
        mockSource.AddSection("TestSettings", settings);

        var manager = new SettingsManager(new[] { mockSource }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var service = new TestSettingsService(manager, this.LoggerFactory);
        await service.InitializeAsync();

        // Act
        var errors = await service.ValidateAsync();

        // Assert
        _ = errors.Should().NotBeEmpty();
        _ = errors.Should().Contain(e => e.PropertyName == "Name");
    }
}
