// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.UI.Xaml;

namespace DroidNet.Converters.Tests.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Dictionary Value")]
[TestCategory("UITest")]
public class DictionaryValueConverterTest
{
    private readonly DictionaryValueConverter<int> converter = new();

    [TestMethod]
    public void Convert_ReturnsValue_ForExistingKey()
    {
        // Arrange
        var dictionary = new Dictionary<string, int>(StringComparer.Ordinal)
        {
            { "apple", 1 },
            { "banana", 2 },
            { "cherry", 3 },
        };
        const string key = "banana";

        // Act
        var result = this.converter.Convert(dictionary, typeof(int), key, "en-US");

        // Assert
        _ = result.Should().Be(2);
    }

    [TestMethod]
    public void Convert_ReturnsNull_ForNullDictionary()
    {
        // Arrange
        const string key = "grape";

        // Act
        var result = this.converter.Convert(value: null, typeof(int), key, "en-US");

        // Assert
        _ = result.Should().BeNull();
    }

    [TestMethod]
    public void Convert_ReturnsNull_ForNullKey()
    {
        // Arrange
        var dictionary = new Dictionary<string, int>(StringComparer.Ordinal)
        {
            { "apple", 1 },
            { "banana", 2 },
            { "cherry", 3 },
        };

        // Act
        var result = this.converter.Convert(dictionary, typeof(int), parameter: null, "en-US");

        // Assert
        _ = result.Should().BeNull();
    }

    [TestMethod]
    public void Convert_ReturnsNull_ForNonexistentKey()
    {
        // Arrange
        var dictionary = new Dictionary<string, int>(StringComparer.Ordinal)
        {
            { "apple", 1 },
            { "banana", 2 },
            { "cherry", 3 },
        };
        const string key = "grape";

        // Act
        var result = this.converter.Convert(dictionary, typeof(int), key, "en-US");

        // Assert
        _ = result.Should().BeNull();
    }

    [TestMethod]
    public void ConvertBack_Should_Throw_InvalidOperationException()
    {
        // Arrange
        const Visibility value = Visibility.Visible;

        // Act & Assert
        Action action = () => this.converter.ConvertBack(value, typeof(object), parameter: null, "en-US");
        _ = action.Should()
            .Throw<InvalidOperationException>();
    }
}
