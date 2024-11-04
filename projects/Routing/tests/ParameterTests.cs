// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Tests;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[TestCategory("Parameters")]
[ExcludeFromCodeCoverage]
public class ParameterTests
{
    [TestMethod]
    public void Constructor_Should_Set_Name_And_Value()
    {
        // Act
        var parameter = new Parameter("param1", "value1");

        // Assert
        parameter.Name.Should().Be("param1");
        parameter.Value.Should().Be("value1");
    }

    [TestMethod]
    public void Equals_Should_Return_True_When_Parameters_Are_Equal()
    {
        // Arrange
        // Use initializers this time, just for coverage
        var parameter1 = new Parameter()
        {
            Name = "param1",
            Value = "value1",
        };
        var parameter2 = new Parameter()
        {
            Name = "param1",
            Value = "value1",
        };

        // Act
        var result = parameter1.Equals(parameter2);

        // Assert
        result.Should().BeTrue();
    }

    [TestMethod]
    public void Equals_Should_Return_False_When_Parameters_Are_Not_Equal()
    {
        // Arrange
        var parameter1 = new Parameter("param1", "value1");
        var parameter2 = new Parameter("param1", "value2");

        // Act
        var result = parameter1.Equals(parameter2);

        // Assert
        result.Should().BeFalse();
    }

    [TestMethod]
    public void GetHashCode_Should_Return_Same_HashCode_For_Equal_Parameters()
    {
        // Arrange
        var parameter1 = new Parameter("param1", "value1");
        var parameter2 = new Parameter("param1", "value1");

        // Act
        var hashCode1 = parameter1.GetHashCode();
        var hashCode2 = parameter2.GetHashCode();

        // Assert
        hashCode1.Should().Be(hashCode2);
    }

    [TestMethod]
    public void ToString_Should_Return_Name_And_Value()
    {
        // Arrange
        var parameter = new Parameter("param1", "value1");

        // Act
        var result = parameter.ToString();

        // Assert
        result.Should().Be("param1=value1");
    }

    [TestMethod]
    public void ToString_Should_Return_Name_Only_When_Value_Is_Null()
    {
        // Arrange
        var parameter = new Parameter("param1", Value: null);

        // Act
        var result = parameter.ToString();

        // Assert
        result.Should().Be("param1");
    }
}
