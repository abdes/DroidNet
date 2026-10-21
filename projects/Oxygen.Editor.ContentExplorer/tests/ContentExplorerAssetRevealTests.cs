// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>Assets are shown in the Content Explorer the window has, and only then does the panel come forward.</summary>
[TestClass]
public sealed class ContentExplorerAssetRevealTests
{
    /// <summary>With no Content Explorer in the window nothing is shown and no panel comes forward.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task RevealWithoutAnExplorerShowsNothingAsync()
    {
        var placement = new Mock<IPlacementService>();

        var shown = await new ContentExplorerAssetReveal(placement.Object).RevealAsync([AssetPath.Parse("/Content/Materials/Red.omat")]).ConfigureAwait(true);

        _ = shown.Should().BeFalse();
        placement.VerifyNoOtherCalls();
    }

    /// <summary>A request that names no asset shows nothing.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task RevealOfNoAssetsShowsNothingAsync()
    {
        var shown = await new ContentExplorerAssetReveal(Mock.Of<IPlacementService>()).RevealAsync([]).ConfigureAwait(true);

        _ = shown.Should().BeFalse();
    }
}
