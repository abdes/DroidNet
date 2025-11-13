// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Editor.Projects.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Categories")]
public class CategoryTests
{
    [TestMethod]
    public void AllCategories_ShouldHaveLocalizedNameAndDescription()
    {
        // Arrange
        var categories = typeof(Category)
            .GetField("Categories", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)
            ?.GetValue(null) as Category[];

        // Act & Assert
        _ = categories.Should().NotBeNull();
        foreach (var category in categories!)
        {
            _ = category.Name.Should().NotStartWith("PROJ_");
            _ = category.Description.Should().NotStartWith("PROJ_");
        }
    }

    [DataTestMethod]
    [DataRow("C44E7604-B265-40D8-9442-11A01ECE334C")]
    [DataRow("D88D97B6-9F2A-4EF5-8137-CD6709CA1233")]
    [DataRow("892D3C51-72C0-47DD-AF32-65CA63EEDDFE")]
    public void ById_ShouldReturnCorrectCategory(string id)
    {
        // Act
        var category = Category.ById(id);

        // Assert
        _ = category.Should().NotBeNull();
        _ = category?.Id.Should().Be(id);
    }
}
