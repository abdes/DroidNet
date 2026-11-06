// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

/// <summary>Checks that one identity is one row, independently of browser scope.</summary>
[TestClass]
public sealed class CookedAssetResolutionTests
{
    /// <summary>A cooked record of an identity stands for it over an authored one, the last mounted when there are several.</summary>
    [TestMethod]
    public void TheLastCookedRecordOfAnIdentityIsItsRow()
    {
        var authored = new AssetRecord(AssetPath.Parse("/Content/Shared.omat"));
        var first = Create("First", "/Content/Shared.omat", new(1, 2));
        var last = Create("Last", "/Content/Shared.omat", new(1, 2));

        _ = CookedAssetResolution.Resolve([authored, first, last], new(AssetQueryScope.All)).Single().Should().Be(last);
        _ = CookedAssetResolution.Resolve([authored], new(AssetQueryScope.All)).Single().Should().Be(authored);
    }

    /// <summary>A query applies to identities: a library's assets are found under its mount.</summary>
    [TestMethod]
    public void AQueryAppliesToIdentities()
    {
        var props = Create("Props", "/Props/Shared.omat", new(1, 2));
        var vehicles = Create("Vehicles", "/Vehicles/Shared.omat", new(3, 4));
        var query = new AssetQuery(new AssetQueryScope([AssetPath.Parse("/Props")], AssetQueryTraversal.Descendants));

        _ = CookedAssetResolution.Resolve([props, vehicles], query).Should().Equal(props);
    }

    private static AssetRecord Create(string root, string path, AssetKey key) => new(AssetPath.Parse(path))
    {
        Cooked = new(FileSystemPath.From(Path.Combine(Path.GetTempPath(), root)), "Materials/Shared.omat", Guid.CreateVersion7(), key, 1, 1, new string('0', 64)) { VirtualPath = AssetPath.Parse(path) },
    };
}
