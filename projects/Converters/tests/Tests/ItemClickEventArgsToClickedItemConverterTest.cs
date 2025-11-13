// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.UI.Xaml.Controls;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;

namespace DroidNet.Converters.Tests.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
[TestCategory("ItemClickEvent Args")]
public class ItemClickEventArgsToClickedItemConverterTest
{
    private readonly ItemClickEventArgsToClickedItemConverter converter = new();

    [UITestMethod]
    public void Convert_ShouldReturnClickedItem_WhenValueIsItemClickEventArgs()
    {
        // Arrange
        var args = new ItemClickEventArgs();

        // Act
        var result = this.converter.Convert(args, typeof(object), parameter: null, "en-US");

        // Assert
        _ = result.Should().Be(args.ClickedItem);
    }

    [TestMethod]
    public void Convert_ShouldThrowArgumentException_WhenValueIsNotItemClickEventArgs()
    {
        // Arrange
        var invalidValue = new object();

        // Act
        Action act = () => this.converter.Convert(invalidValue, typeof(object), parameter: null, "en-US");

        // Assert
        _ = act.Should()
            .Throw<ArgumentException>()
            .WithMessage("ItemClickEventArgsToClickedItemConverter can only convert ItemClickEventArgs values*")
            .And.ParamName.Should()
            .Be("value");
    }

    [TestMethod]
    public void ConvertBack_ShouldThrowInvalidOperationException()
    {
        // Arrange
        var value = new object();

        // Act
        Action act = () => this.converter.ConvertBack(value, typeof(object), parameter: null, "en-US");

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }
}
