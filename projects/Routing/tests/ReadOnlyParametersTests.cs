// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

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
    public void Contains_Should_Return_True_When_Parameter_Exists()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");
        var readOnlyParameters = parameters.AsReadOnly();

        // Act
        var result = readOnlyParameters.Contains("param1");

        // Assert
        _ = result.Should().BeTrue();
    }

    [TestMethod]
    public void Contains_Should_Return_False_When_Parameter_Not_Exists()
    {
        // Arrange
        var parameters = new Parameters();
        var readOnlyParameters = parameters.AsReadOnly();

        // Act
        var result = readOnlyParameters.Contains("param1");

        // Assert
        _ = result.Should().BeFalse();
    }

    [TestMethod]
    public void TryGetValue_Should_Return_True_And_Value_When_Parameter_Exists()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");
        var readOnlyParameters = parameters.AsReadOnly();

        // Act
        var result = readOnlyParameters.TryGetValue("param1", out var value);

        // Assert
        _ = result.Should().BeTrue();
        _ = value.Should().Be("value1");
    }

    [TestMethod]
    public void TryGetValue_Should_Return_False_When_Parameter_Not_Exists()
    {
        // Arrange
        var parameters = new Parameters();
        var readOnlyParameters = parameters.AsReadOnly();

        // Act
        var result = readOnlyParameters.TryGetValue("param1", out var value);

        // Assert
        _ = result.Should().BeFalse();
        _ = value.Should().BeNull();
    }

    [TestMethod]
    public void GetEnumerator_Should_Return_All_Parameters()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");
        parameters.AddOrUpdate("param2", "value2");
        var readOnlyParameters = parameters.AsReadOnly();

        // Act
        var paramList = readOnlyParameters.ToList();

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
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");
        parameters.AddOrUpdate("param2", "value2");
        var readOnlyParameters = parameters.AsReadOnly();

        // Act
        var count = CountAll(readOnlyParameters);

        // Assert
        _ = count.Should().Be(readOnlyParameters.Count);
    }

    private static int CountAll(IEnumerable collection) => collection.Cast<object?>().Count();
}
