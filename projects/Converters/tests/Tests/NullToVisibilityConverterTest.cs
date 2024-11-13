// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Converters.Tests;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.UI.Xaml;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Null To Visibility")]
public class NullToVisibilityConverterTest
{
    private readonly NullToVisibilityConverter converter = new();

    [TestMethod]
    public void NotNullValue_ProducesVisible()
    {
        // Arrange
        const string nonNullValue = "Hello";

        // Act
        var result = this.converter.Convert(nonNullValue, typeof(Visibility), parameter: null, "en-US");

        // Assert
        _ = result.Should().Be(Visibility.Visible);
    }

    [TestMethod]
    public void NullValue_ProducesCollapsed()
    {
        // Arrange
        object? nullValue = null;

        // Act
        var result = this.converter.Convert(nullValue, typeof(Visibility), parameter: null, "en-US");

        // Assert
        result.Should().Be(Visibility.Collapsed);
    }

    [TestMethod]
    public void Return_CustomVisibility_WhenProvided()
    {
        // Arrange
        const Visibility customInvisibility = Visibility.Collapsed;

        // Act
        var result = this.converter.Convert(value: null, typeof(Visibility), customInvisibility.ToString(), "en-US");

        // Assert
        _ = result.Should().Be(customInvisibility);
    }

    [TestMethod]
    public void Convert_Should_Handle_Invalid_Parameter()
    {
        // Arrange
        const string invalidParameter = "InvalidVisibility";

        // Act
        var result = this.converter.Convert(value: null, typeof(Visibility), invalidParameter, "en-US");

        // Assert
        _ = result.Should().Be(Visibility.Collapsed); // Default fallback
    }

    [TestMethod]
    public void ConvertBack_ShouldThrow_InvalidOperationException()
    {
        // Arrange
        const Visibility value = Visibility.Visible;

        // Act & Assert
        Action action = () => this.converter.ConvertBack(value, typeof(object), parameter: null, "en-US");
        _ = action.Should().Throw<InvalidOperationException>();
    }
}
