// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Tests;

using System.Collections;
using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[TestCategory("Parameters")]
[ExcludeFromCodeCoverage]
public class ReadOnlyParametersTests
{
    [TestMethod]
    public void IsEmpty_ShouldReturnTrue_WhenCollectionIsEmpty()
    {
        // Arrange
        var parameters = new Parameters().AsReadOnly();

        // Act
        var isEmpty = parameters.IsEmpty;

        // Assert
        isEmpty.Should().BeTrue();
    }

    [TestMethod]
    public void IsEmpty_ShouldReturnFalse_WhenCollectionIsNotEmpty()
    {
        // Arrange
        var parameters = new Parameters
        {
            { "Param1", "Value1" },
        }.AsReadOnly();

        // Act
        var isEmpty = parameters.IsEmpty;

        // Assert
        isEmpty.Should().BeFalse();
    }

    [TestMethod]
    public void Contains_Should_Return_True_When_Parameter_Exists()
    {
        // Arrange
        var parameters = new Parameters
        {
            { "param1", "value1" },
        }.AsReadOnly();

        // Act
        var result = parameters.Contains("param1");

        // Assert
        _ = result.Should().BeTrue();
    }

    [TestMethod]
    public void Contains_Should_Return_False_When_Parameter_Not_Exists()
    {
        // Arrange
        var parameters = new Parameters().AsReadOnly();

        // Act
        var result = parameters.Contains("param1");

        // Assert
        _ = result.Should().BeFalse();
    }

    [TestMethod]
    public void TryGetValue_Should_Return_True_And_Value_When_Parameter_Exists()
    {
        // Arrange
        var parameters = new Parameters
        {
            { "param1", "value1" },
        }.AsReadOnly();

        // Act
        var result = parameters.TryGetValue("param1", out var value);

        // Assert
        _ = result.Should().BeTrue();
        _ = value.Should().Be("value1");
    }

    [TestMethod]
    public void TryGetValue_Should_Return_False_When_Parameter_Not_Exists()
    {
        // Arrange
        var parameters = new Parameters().AsReadOnly();

        // Act
        var result = parameters.TryGetValue("param1", out var value);

        // Assert
        _ = result.Should().BeFalse();
        _ = value.Should().BeNull();
    }

    [TestMethod]
    public void GetEnumerator_Should_Return_All_Parameters()
    {
        // Arrange
        var parameters = new Parameters
        {
            { "param1", "value1" },
            { "param2", "value2" },
        }.AsReadOnly();

        // Act
        var paramList = parameters.ToList();

        _ = paramList.Should().HaveCount(2);
        _ = paramList.Should().Contain(new Parameter("param1", "value1"));
        _ = paramList.Should().Contain(new Parameter("param2", "value2"));
    }

    [TestMethod]
    public void ReadOnlyParameters_Empty_Should_Be_Empty() =>

        // Act & Assert
        ReadOnlyParameters.Empty.Count.Should().Be(0);

    [TestMethod]
    public void GenericGetEnumerator_Should_Return_All_Parameters()
    {
        var parameters = new Parameters
        {
            { "param1", "value1" },
            { "param2", "value2" },
        }.AsReadOnly();

        // Act
        var count = CountAll(parameters);

        // Assert
        _ = count.Should().Be(parameters.Count);
    }

    private static int CountAll(IEnumerable collection) => collection.Cast<object?>().Count();
}
