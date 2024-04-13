// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

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
