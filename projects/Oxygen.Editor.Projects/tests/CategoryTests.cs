// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Windows.ApplicationModel.DynamicDependency;

namespace Oxygen.Editor.Projects.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Categories")]
public class CategoryTests
{
    [TestMethod]
    [DataRow("C44E7604-B265-40D8-9442-11A01ECE334C")]
    [DataRow("D88D97B6-9F2A-4EF5-8137-CD6709CA1233")]
    [DataRow("892D3C51-72C0-47DD-AF32-65CA63EEDDFE")]
    public void ById_ShouldReturnCorrectCategory(string id)
    {
        // Act
        var category = Category.ById(id);

        // Assert
        _ = category.Should().NotBeNull();
        _ = category.Id.Should().Be(id);
    }
}
