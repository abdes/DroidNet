// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Persistence;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public sealed class AssetKeyTests
{
    /// <summary>The key the engine gave the cube in its content pak.</summary>
    [TestMethod]
    public void FromVirtualPath_ShouldDeriveTheEngineKey()
    {
        var key = AssetKey.FromVirtualPath("/Engine/Generated/BasicShapes/Cube.ogeo");

        _ = key.ToString().Should().Be("5674ea56-04e4-2809-20f0-32e4fbfb728a");
    }
}
