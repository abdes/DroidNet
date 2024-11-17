// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.Docking.Tests;

[TestClass]
[TestCategory(nameof(ToolDock))]
[ExcludeFromCodeCoverage]
public class ToolDockTests
{
    [TestMethod]
    public void CanCreate()
    {
        // Arrange / Act
        using var dock = ToolDock.New();

        // Assert
        _ = dock.Should().NotBeNull();
    }
}
