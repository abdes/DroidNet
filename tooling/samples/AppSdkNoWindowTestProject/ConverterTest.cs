// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Tests;

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
