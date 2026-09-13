// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Managed.Assets.Tests;

/// <summary>Checks native path-then-key precedence independently of browser scope.</summary>
[TestClass]
public sealed class CookedAssetResolutionTests
{
    /// <summary>Reversing source order changes the winner and retains the masked representation.</summary>
    [TestMethod]
    public void LastMountedPathWinsAndRetainsOverriddenSources()
    {
        var first = Create("First", "/Content/Shared.omat", new(1, 2));
        var last = Create("Last", "/Content/Shared.omat", new(3, 4));
        var forward = CookedAssetResolution.Resolve([first, last], new(AssetQueryScope.All)).Single();
        _ = forward.Cooked.Should().Be(last.Cooked);
        _ = forward.OverriddenCookedSources.Should().Equal(first.Cooked!);
        var reversed = CookedAssetResolution.Resolve([last, first], new(AssetQueryScope.All)).Single();
        _ = reversed.Cooked.Should().Be(first.Cooked);
        _ = reversed.OverriddenCookedSources.Should().Equal(last.Cooked!);
    }

    /// <summary>A higher-priority key definition remains effective even when its own alias is outside the queried folder.</summary>
    [TestMethod]
    public void KeyResolutionPrecedesFolderFilteringAndPreservesTheRequestedAlias()
    {
        var first = Create("First", "/Content/Original/Shared.omat", new(1, 2));
        var pathWinner = Create("Middle", "/Content/Original/Shared.omat", new(3, 4));
        var keyWinner = Create("Last", "/Content/Replacement/Other.omat", new(3, 4));
        var query = new AssetQuery(new AssetQueryScope([new("asset:///Content/Original")], AssetQueryTraversal.Descendants));
        var resolved = CookedAssetResolution.Resolve([first, pathWinner, keyWinner], query).Single();
        _ = resolved.Uri.Should().Be(first.Uri);
        _ = resolved.Cooked.Should().Be(keyWinner.Cooked);
        _ = resolved.OverriddenCookedSources.Should().BeEquivalentTo(new[] { first.Cooked, pathWinner.Cooked });
    }

    private static AssetRecord Create(string root, string path, AssetKey key) => new(new Uri("asset://" + path))
    {
        Cooked = new(root, "Materials/Shared.omat", Guid.CreateVersion7(), key, 1, 1, new string('0', 64)) { VirtualPath = path },
    };
}
