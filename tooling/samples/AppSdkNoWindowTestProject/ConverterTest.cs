// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.Tests;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
public class ConverterTest
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
}

/// <summary>Convert a <see langword="null" /> value to a <see cref="Visibility" /> value.</summary>
public partial class NullToVisibilityConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, string language)
    {
        var invisibility = Visibility.Collapsed;

        if (parameter is string preferredInvisibility && Enum.TryParse(
                typeof(Visibility),
                preferredInvisibility,
                ignoreCase: true,
                out var result))
        {
            invisibility = (Visibility)result;
        }

        return value == null ? invisibility : Visibility.Visible;
    }

    public object ConvertBack(object value, Type targetType, object? parameter, string language)
        => throw new InvalidOperationException("Don't use NullToVisibilityConverter.ConvertBack; it's meaningless.");
}
