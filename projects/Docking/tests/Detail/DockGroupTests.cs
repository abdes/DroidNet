// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Mocks;
using DroidNet.TestHelpers;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Contains unit test cases for the <see cref="DockGroup" /> class general
/// features.
/// </summary>
[TestClass]
[TestCategory(nameof(DockGroup))]
[ExcludeFromCodeCoverage]
public partial class DockGroupTests : TestSuiteWithAssertions
{
    private readonly DummyDocker docker = new();

    [TestCleanup]
    public new void Dispose()
    {
        this.docker.Dispose();
        base.Dispose();
        GC.SuppressFinalize(this);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.General")]
    public void BaseDockGroup_Ctor_StartsWithNullParts()
    {
        // Arrange & Act
        var group = new MockDockGroup(this.docker);

        // Assert
        _ = group.First.Should().BeNull();
        _ = group.Second.Should().BeNull();
    }
}
