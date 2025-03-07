// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.Routing.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(OutletName))]
public class OutletNameTests
{
    [TestMethod]
    public void ImplicitStringToOutletName_ShouldConvert()
    {
        // Arrange
        const string outletString = "TestOutlet";

        // Act
        OutletName outletName = outletString;

        // Assert
        _ = outletName.Name.Should().Be(outletString);
    }

    [TestMethod]
    public void ImplicitOutletNameToString_ShouldConvert()
    {
        // Arrange / Act
        string result = new OutletName { Name = "TestOutlet" };

        // Assert
        _ = result.Should().Be("TestOutlet");
    }

    [TestMethod]
    public void IsPrimary_ShouldReturnTrue_WhenOutletNameIsPrimary()
    {
        // Arrange
        var outletName = OutletName.Primary;

        // Act
        var isPrimary = outletName.IsPrimary;

        // Assert
        _ = isPrimary.Should().BeTrue();
    }

    [TestMethod]
    public void IsNotPrimary_ShouldReturnTrue_WhenOutletNameIsNotPrimary()
    {
        // Arrange
        var outletName = new OutletName { Name = "TestOutlet" };

        // Act
        var isNotPrimary = outletName.IsNotPrimary;

        // Assert
        _ = isNotPrimary.Should().BeTrue();
    }

    [TestMethod]
    public void EqualityComparer_ShouldIgnoreCase_WhenFlagIsTrue()
    {
        // Arrange
        var comparer = OutletNameEqualityComparer.IgnoreCase;

        // ReSharper disable once StringLiteralTypo
        var name1 = new OutletName { Name = "testoutlet" };
        var name2 = new OutletName { Name = "TestOutlet" };

        // Act
        var areEqual = comparer.Equals(name1, name2);

        // Assert
        _ = areEqual.Should().BeTrue();
    }

    [TestMethod]
    public void ToString_ShouldReturnTheOutletName()
    {
        // Arrange
        const string name = "TestOutlet";
        var outletName = new OutletName { Name = name };

        // Act
        var toStringValue = outletName.ToString();

        // Assert
        _ = toStringValue.Should().Be(name);
    }

    [TestMethod]
    public void EqualityComparer_ShouldConsiderCase_WhenFlagIsFalse()
    {
        // Arrange
        var comparer = new OutletNameEqualityComparer(ignoreCase: false);

        // ReSharper disable once StringLiteralTypo
        var name1 = new OutletName { Name = "testoutlet" };
        var name2 = new OutletName { Name = "TestOutlet" };

        // Act
        var areEqual = comparer.Equals(name1, name2);

        // Assert
        _ = areEqual.Should().BeFalse();
    }

    [TestMethod]
    public void EqualityComparer_ShouldReturnTrue_WhenBothValuesAreSameInstance()
    {
        // Arrange
        var comparer = new OutletNameEqualityComparer();
        var name = new OutletName { Name = "TestOutlet" };

        // Act
        var areEqual = comparer.Equals(name, name);

        // Assert
        _ = areEqual.Should().BeTrue();
    }

    [TestMethod]
    public void EqualityComparer_ShouldReturnFalse_WhenOneValueIsNull()
    {
        // Arrange
        var comparer = new OutletNameEqualityComparer();
        var name = new OutletName { Name = "TestOutlet" };

        // Act & Assert
        _ = comparer.Equals(name, y: null).Should().BeFalse();
        _ = comparer.Equals(x: null, name).Should().BeFalse();
    }

    [TestMethod]
    public void EqualityComparer_ShouldReturnTrue_WhenBothValuesAreNull()
    {
        // Arrange
        var comparer = new OutletNameEqualityComparer();

        // Act
        var areEqual = comparer.Equals(x: null, y: null);

        // Assert
        _ = areEqual.Should().BeTrue();
    }

    [TestMethod]
    public void GetHashCode_ShouldMatch_WhenNamesAreEqual()
    {
        // Arrange
        var comparer = new OutletNameEqualityComparer();
        var name1 = new OutletName { Name = "TestOutlet" };
        var name2 = new OutletName { Name = "TestOutlet" };

        // Act
        var hash1 = comparer.GetHashCode(name1);
        var hash2 = comparer.GetHashCode(name2);

        // Assert
        _ = hash1.Should().Be(hash2);
    }

    [TestMethod]
    public void GetHashCode_ShouldNotMatch_WhenNamesAreDifferent()
    {
        // Arrange
        var comparer = new OutletNameEqualityComparer();
        var name1 = new OutletName { Name = "TestOutlet1" };
        var name2 = new OutletName { Name = "TestOutlet2" };

        // Act
        var hash1 = comparer.GetHashCode(name1);
        var hash2 = comparer.GetHashCode(name2);

        // Assert
        _ = hash1.Should().NotBe(hash2);
    }
}
