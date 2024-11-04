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
public class ParametersTests
{
    [TestMethod]
    public void IsEmpty_ShouldReturnTrue_WhenCollectionIsEmpty()
    {
        // Arrange
        var parameters = new Parameters();

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
        };

        // Act
        var isEmpty = parameters.IsEmpty;

        // Assert
        isEmpty.Should().BeFalse();
    }

    [TestMethod]
    public void AddOrUpdate_Should_Add_Parameter_When_Not_Exists()
    {
        // Arrange
        var parameters = new Parameters();

        // Act
        parameters.AddOrUpdate("param1", "value1");

        // Assert
        _ = parameters.Count.Should().Be(1);
        _ = parameters.TryGetValue("param1", out var value).Should().BeTrue();
        _ = value.Should().Be("value1");
    }

    [TestMethod]
    public void AddOrUpdate_Should_Update_Parameter_When_Exists()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");

        // Act
        parameters.AddOrUpdate("param1", "value2");

        // Assert
        _ = parameters.Count.Should().Be(1);
        _ = parameters.TryGetValue("param1", out var value).Should().BeTrue();
        _ = value.Should().Be("value2");
    }

    [TestMethod]
    public void TryAdd_Should_Return_True_When_Parameter_Added()
    {
        // Arrange
        var parameters = new Parameters();

        // Act
        var result = parameters.TryAdd("param1", "value1");

        // Assert
        _ = result.Should().BeTrue();
        _ = parameters.Count.Should().Be(1);
    }

    [TestMethod]
    public void TryAdd_Should_Return_False_When_Parameter_Already_Exists()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");

        // Act
        var result = parameters.TryAdd("param1", "value2");

        // Assert
        _ = result.Should().BeFalse();
        _ = parameters.TryGetValue("param1", out var value).Should().BeTrue();
        _ = value.Should().Be("value1");
    }

    [TestMethod]
    public void AddOrUpdate_Should_Throw_ArgumentException_When_Name_Is_Empty()
    {
        // Arrange
        var parameters = new Parameters();

        // Act
        var act = () => parameters.AddOrUpdate(string.Empty, "value1");

        // Assert
        _ = act.Should()
            .Throw<ArgumentException>()
            .WithMessage("Segment parameters cannot have an empty name. (Parameter 'name')");
    }

    [TestMethod]
    public void TryAdd_Should_Throw_ArgumentException_When_Name_Is_Empty()
    {
        // Arrange
        var parameters = new Parameters();

        // Act
        Action act = () => parameters.TryAdd(string.Empty, "value1");

        // Assert
        _ = act.Should()
            .Throw<ArgumentException>()
            .WithMessage("Segment parameters cannot have an empty name. (Parameter 'name')");
    }

    [TestMethod]
    public void Add_Should_Add_Parameter_When_Name_Is_Valid_And_Not_Exists()
    {
        // Arrange
        Parameters parameters = [];

        // Act
        parameters.Add("param1", "value1");

        // Assert
        _ = parameters.Contains("param1").Should().BeTrue();
        _ = parameters.TryGetValue("param1", out var value).Should().BeTrue();
        _ = value.Should().Be("value1");
    }

    [TestMethod]
    public void Add_Should_Throw_ArgumentException_When_Name_Is_Empty()
    {
        // Arrange
        var parameters = new Parameters();

        // Act
        var act = () => parameters.Add(string.Empty, "value1");

        // Assert
        _ = act.Should()
            .Throw<ArgumentException>()
            .WithMessage("Segment parameters cannot have an empty name. (Parameter 'name')");
    }

    [TestMethod]
    public void Add_Should_Throw_ArgumentException_When_Name_Already_Exists()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");

        // Act
        var act = () => parameters.Add("param1", "value2");

        // Assert
        _ = act.Should()
            .Throw<ArgumentException>()
            .WithMessage("An item with the same key has already been added. Key: param1");
    }

    [TestMethod]
    public void Remove_Should_Return_True_When_Parameter_Removed()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");

        // Act
        var result = parameters.Remove("param1");

        // Assert
        _ = result.Should().BeTrue();
        _ = parameters.Count.Should().Be(0);
    }

    [TestMethod]
    public void Remove_Should_Return_False_When_Parameter_Not_Found()
    {
        // Arrange
        var parameters = new Parameters();

        // Act
        var result = parameters.Remove("param1");

        // Assert
        _ = result.Should().BeFalse();
    }

    [TestMethod]
    public void Clear_Should_Remove_All_Parameters()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");
        parameters.AddOrUpdate("param2", "value2");

        // Act
        parameters.Clear();

        // Assert
        _ = parameters.Count.Should().Be(0);
    }

    [TestMethod]
    public void AsReadOnly_Should_Return_ReadOnlyParameters()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");

        // Act
        var readOnlyParameters = parameters.AsReadOnly();

        // Assert
        _ = readOnlyParameters.Should().BeOfType<ReadOnlyParameters>();
        _ = readOnlyParameters.Count.Should().Be(1);
    }

    [TestMethod]
    public void Contains_Should_Return_True_When_Parameter_Exists()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");

        // Act
        var result = parameters.Contains("param1");

        // Assert
        _ = result.Should().BeTrue();
    }

    [TestMethod]
    public void Contains_Should_Return_False_When_Parameter_Not_Exists()
    {
        // Arrange
        var parameters = new Parameters();

        // Act
        var result = parameters.Contains("param1");

        // Assert
        _ = result.Should().BeFalse();
    }

    [TestMethod]
    public void GetValues_ShouldReturnNull_WhenParameterDoesNotExist()
    {
        // Arrange
        var parameters = new Parameters();

        // Act
        var result = parameters.GetValues("NonExistentParameter");

        // Assert
        result.Should().BeNull();
    }

    [TestMethod]
    public void GetValues_ShouldReturnEmptyArray_WhenParameterHasNoValue()
    {
        // Arrange
        var parameters = new Parameters
        {
            { "EmptyParam", null },
        };

        // Act
        var result = parameters.GetValues("EmptyParam");

        // Assert
        result.Should().NotBeNull().And.BeEmpty();
    }

    [TestMethod]
    public void GetValues_ShouldReturnEmptyString_WhenParameterValueIsEmpty()
    {
        // Arrange
        var parameters = new Parameters
        {
            { "EmptyStringParam", string.Empty },
        };

        // Act
        var result = parameters.GetValues("EmptyStringParam");

        // Assert
        result.Should().NotBeNull();
        result.Should().ContainSingle().Which.Should().BeEmpty();
    }

    [TestMethod]
    public void GetValues_ShouldReturnSingleValue_WhenParameterHasSingleValue()
    {
        // Arrange
        var parameters = new Parameters
        {
            { "SingleValueParam", "Value1" },
        };

        // Act
        var result = parameters.GetValues("SingleValueParam");

        // Assert
        result.Should().NotBeNull();
        result.Should().ContainSingle().Which.Should().Be("Value1");
    }

    [TestMethod]
    public void GetValues_ShouldReturnMultipleValues_WhenParameterHasCommaSeparatedValues()
    {
        // Arrange
        var parameters = new Parameters
        {
            { "MultipleValuesParam", "Value1,Value2,Value3" },
        };

        // Act
        var result = parameters.GetValues("MultipleValuesParam");

        // Assert
        result.Should().NotBeNull();
        result.Should().ContainInOrder("Value1", "Value2", "Value3");
    }

    [TestMethod]
    public void GenericGetEnumerator_Should_Return_All_Parameters()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");
        parameters.AddOrUpdate("param2", "value2");

        // Act
        var count = CountAll(parameters);

        // Assert
        _ = count.Should().Be(parameters.Count);
    }

    [TestMethod]
    public void Parameters_IsEnumerable()
    {
        // Arrange
        var parameters = new Parameters();
        parameters.AddOrUpdate("param1", "value1");
        parameters.AddOrUpdate("param2", "value2");

        // Act
        var paramList = parameters.ToList();

        // Assert
        _ = paramList.Should().HaveCount(2);
        _ = paramList.Should().Contain(new Parameter("param1", "value1"));
        _ = paramList.Should().Contain(new Parameter("param2", "value2"));
    }

    private static int CountAll(IEnumerable collection) => collection.Cast<object?>().Count();
}
