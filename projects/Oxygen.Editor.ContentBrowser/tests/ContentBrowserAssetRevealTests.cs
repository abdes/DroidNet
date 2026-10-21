// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Assets are shown in the Content Browser the window has, and only then does the panel come forward.</summary>
[TestClass]
public sealed class ContentBrowserAssetRevealTests
{
    /// <summary>With no Content Browser in the window nothing is shown and no panel comes forward.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task RevealWithoutABrowserShowsNothingAsync()
    {
        var placement = new Mock<IPlacementService>();

        var shown = await new ContentBrowserAssetReveal(placement.Object).RevealAsync([AssetPath.Parse("/Content/Materials/Red.omat")]).ConfigureAwait(true);

        _ = shown.Should().BeFalse();
        placement.VerifyNoOtherCalls();
    }

    /// <summary>A request that names no asset shows nothing.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task RevealOfNoAssetsShowsNothingAsync()
    {
        var shown = await new ContentBrowserAssetReveal(Mock.Of<IPlacementService>()).RevealAsync([]).ConfigureAwait(true);

        _ = shown.Should().BeFalse();
    }
}
