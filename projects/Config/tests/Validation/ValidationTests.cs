// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Config.Tests.Helpers;
using DryIoc;

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
    private readonly MockSettingsSource source = new("test-source");

    [TestInitialize]
    public void TestInitialize() => this.Container.RegisterInstance<ISettingsSource>(this.source);

    [TestMethod]
    public async Task ValidateAsync_WithAllValidProperties_ShouldReturnNoErrors()
    {
        // Arrange
        var validSettings = new TestSettings
        {
            Name = "ValidName",
            Value = 50,
            IsEnabled = true,
        };
        this.source.AddSection("TestSettings", validSettings);
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        using var service = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Act
        var errors = await service.ValidateAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = errors.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ValidateAsync_WithRequiredFieldViolation_ShouldReturnError()
    {
        // Arrange
        var invalidSettings = new TestSettingsWithValidation
        {
            RequiredField = null, // Required field violation
        };
        this.source.AddSection("InvalidTestSettings", invalidSettings);
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        using var service = this.Container.Resolve<ISettingsService<ITestSettingsWithValidation>>();

        // Act
        var errors = await service.ValidateAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = errors.Should().NotBeEmpty();
        _ = errors.Should().Contain(e => e.PropertyName == "RequiredField");
        _ = errors.Should().Contain(e => e.Message.Contains("required", StringComparison.OrdinalIgnoreCase));
    }

    [TestMethod]
    public async Task ValidateAsync_WithRangeViolation_ShouldReturnError()
    {
        // Arrange
        var invalidSettings = new TestSettingsWithValidation
        {
            RequiredField = "Present",
            OutOfRangeValue = 999, // Range is 1-10
            InvalidEmail = "valid@email.com",
        };
        this.source.AddSection("InvalidTestSettings", invalidSettings);
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        using var service = this.Container.Resolve<ISettingsService<ITestSettingsWithValidation>>();

        // Act
        var errors = await service.ValidateAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = errors.Should().NotBeEmpty();
        _ = errors.Should().Contain(e => e.PropertyName == "OutOfRangeValue");
        _ = errors.Should().Contain(e => e.Message.Contains("range", StringComparison.OrdinalIgnoreCase)
            || e.Message.Contains('1') || e.Message.Contains("10"));
    }

    [TestMethod]
    public async Task ValidateAsync_WithEmailAddressViolation_ShouldReturnError()
    {
        // Arrange
        var invalidSettings = new TestSettingsWithValidation
        {
            RequiredField = "Present",
            OutOfRangeValue = 5,
            InvalidEmail = "not-an-email", // Invalid email format
        };
        this.source.AddSection("InvalidTestSettings", invalidSettings);
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        using var service = this.Container.Resolve<ISettingsService<ITestSettingsWithValidation>>();

        // Act
        var errors = await service.ValidateAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = errors.Should().NotBeEmpty();
        _ = errors.Should().Contain(e => e.PropertyName == "InvalidEmail");
    }

    [TestMethod]
    public async Task ValidateAsync_WithMultipleViolations_ShouldReturnAllErrors()
    {
        // Arrange
        var invalidSettings = new TestSettingsWithValidation
        {
            RequiredField = null, // Violation 1
            OutOfRangeValue = 999, // Violation 2
            InvalidEmail = "not-an-email", // Violation 3
        };
        this.source.AddSection("InvalidTestSettings", invalidSettings);
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        using var service = this.Container.Resolve<ISettingsService<ITestSettingsWithValidation>>();

        // Act
        var errors = await service.ValidateAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = errors.Should().HaveCountGreaterThanOrEqualTo(2);
        _ = errors.Select(e => e.PropertyName).Should().Contain("RequiredField");
        _ = errors.Select(e => e.PropertyName).Should().Contain("OutOfRangeValue");
    }

    [TestMethod]
    public async Task SaveAsync_WithValidationErrors_ShouldThrowSettingsValidationException()
    {
        // Arrange
        var invalidSettings = new TestSettingsWithValidation
        {
            RequiredField = null,
            OutOfRangeValue = 999,
            InvalidEmail = "not-an-email",
        };
        this.source.AddSection("InvalidTestSettings", invalidSettings);
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        using var service = this.Container.Resolve<ISettingsService<ITestSettingsWithValidation>>();

        // Mark as dirty to trigger save
        var isDirtyProperty = typeof(Config.SettingsService<ITestSettingsWithValidation>).GetProperty("IsDirty");
        isDirtyProperty?.SetValue(service, value: true);

        // Act
        var act = async () => await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        var exception = await act.Should().ThrowAsync<SettingsValidationException>().ConfigureAwait(true);
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
            new("Property2", "Error 2"),
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
    public async Task ValidateAsync_WithNullableProperties_ShouldHandleCorrectly()
    {
        // Arrange
        var settings = new TestSettings
        {
            Name = "Test",
            Value = 50,
            Description = null, // Nullable property
        };
        this.source.AddSection("InvalidTestSettings", settings);
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        using var service = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Act
        var errors = await service.ValidateAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = errors.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ValidateAsync_ShouldValidateAllProperties()
    {
        // Arrange
        var settings = new TestSettings
        {
            Name = "A", // Valid - minimum length 1
            Value = 500, // Valid - within range 0-1000
            IsEnabled = true,
        };
        this.source.AddSection("InvalidTestSettings", settings);
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        using var service = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Act
        var errors = await service.ValidateAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - All properties should pass validation
        _ = errors.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ValidateAsync_WithCancellation_ShouldComplete()
    {
        // Arrange
        this.source.AddSection("TestSettings", new TestSettings());
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        using var service = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Act - Validation doesn't actually support cancellation in current implementation
        var errors = await service.ValidateAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

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
}
