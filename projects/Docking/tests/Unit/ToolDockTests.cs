// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Docking.Workspace;

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
        using var docker = new Docker();
        using var dock = docker.CreateDock<ToolDock>();

        // Assert
        _ = dock.Should().NotBeNull();
    }
}
