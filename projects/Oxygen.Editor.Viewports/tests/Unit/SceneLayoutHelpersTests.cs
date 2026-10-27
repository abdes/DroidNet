// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Editor.Viewports.Unit.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Viewport Layout")]
internal sealed class SceneLayoutHelpersTests
{
    [TestMethod]
    public void GetPlacements_ShouldReturnOnePlacement_ForOnePane()
    {
        var placements = SceneLayoutHelpers.GetPlacements(SceneViewLayout.OnePane);

        _ = placements.Should().ContainSingle();
        _ = placements[0].Should().Be((0, 0, 1, 1));
    }

    [TestMethod]
    public void GetPlacements_ShouldReturnDistinctPlacements_ForTwoMainLeft()
    {
        var placements = SceneLayoutHelpers.GetPlacements(SceneViewLayout.TwoMainLeft);

        _ = placements.Should().Equal([(0, 0, 1, 1), (0, 1, 1, 1)]);
    }

    [TestMethod]
    public void GetPlacements_ShouldReturnFourDistinctPlacements_ForFourQuad()
    {
        var placements = SceneLayoutHelpers.GetPlacements(SceneViewLayout.FourQuad);

        _ = placements.Should().Equal([(0, 0, 1, 1), (0, 1, 1, 1), (1, 0, 1, 1), (1, 1, 1, 1)]);
    }
}
