// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks native cooked facts independently of filename conventions and project output locations.</summary>
public sealed partial class AssetIdentityReducerTests
{
    /// <summary>Indexed type and location drive the browser even when the virtual extension suggests something else.</summary>
    /// <param name="assetType">The native indexed type.</param>
    /// <param name="kind">The expected browser type.</param>
    [TestMethod]
    [DataRow((byte)1, AssetKind.Material)]
    [DataRow((byte)2, AssetKind.Geometry)]
    [DataRow((byte)3, AssetKind.Scene)]
    [DataRow((byte)4, AssetKind.Unknown)]
    [DataRow((byte)255, AssetKind.Unknown)]
    public void CookedRecordsUseIndexedTypeAndPhysicalDescriptor(byte assetType, AssetKind kind)
    {
        using var workspace = new TempWorkspace();
        var root = workspace.SourcePath("Library");
        var physical = workspace.SourcePath("Library/payloads/7.bin");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(physical)!);
        File.WriteAllBytes(physical, [1]);
        var record = new AssetRecord(new Uri("asset:///Content/Wood.png"))
        {
            Cooked = new(root, "payloads/7.bin", Guid.NewGuid(), new AssetKey(1, 2), assetType, 1, new string('0', 64)),
        };
        var row = Reduce(workspace, [record]).Single();
        _ = row.Kind.Should().Be(kind);
        _ = row.CookedPath.Should().Be(physical);
        _ = row.CookedMetadata.Should().Be(record.Cooked);
        _ = row.IdentityUri.Should().Be(record.Uri);
        _ = row.CookedUri.Should().Be(record.Uri);
        _ = row.DescriptorPath.Should().BeNull();
        _ = row.SourcePath.Should().BeNull();
        _ = row.PrimaryState.Should().Be(AssetState.Cooked);
        _ = row.CanCook.Should().BeFalse();
    }

    /// <summary>An invalid indexed location cannot escape its root or become a selectable asset.</summary>
    [TestMethod]
    public void InvalidCookedDescriptorLocationIsReportedOnTheAsset()
    {
        using var workspace = new TempWorkspace();
        var record = new AssetRecord(new Uri("asset:///Content/Shape.ogeo"))
        {
            Cooked = new(workspace.SourcePath("Library"), "../outside.ogeo", Guid.NewGuid(), new AssetKey(1, 2), 2, 1, new string('0', 64)),
        };
        var row = Reduce(workspace, [record]).Single();
        _ = row.CookedPath.Should().BeNull();
        _ = row.PrimaryState.Should().Be(AssetState.Broken);
        _ = row.IsSelectable.Should().BeFalse();
        _ = row.DiagnosticCodes.Should().Contain(AssetIdentityDiagnosticCodes.DescriptorBroken);
    }
}
