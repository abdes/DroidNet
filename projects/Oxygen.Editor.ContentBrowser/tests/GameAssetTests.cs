// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.Models;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class GameAssetTests
{
    [DataTestMethod]
    [DataRow("Foo.oscene.json", AssetType.Scene)]
    [DataRow("Foo.omat.json", AssetType.Material)]
    [DataRow("Foo.ogeo.json", AssetType.Mesh)]
    [DataRow("Foo.otex.json", AssetType.Texture)]
    [DataRow("Foo.oscene", AssetType.Scene)]
    [DataRow("Foo.omat", AssetType.Material)]
    [DataRow("Foo.ogeo", AssetType.Mesh)]
    [DataRow("Foo.otex", AssetType.Texture)]
    [DataRow("Foo.glb", AssetType.ForeignAsset)]
    [DataRow("Foo.gltf", AssetType.ForeignAsset)]
    [DataRow("Foo.fbx", AssetType.ForeignAsset)]
    [DataRow("Foo.png", AssetType.Image)]
    public void GetAssetType_ShouldHandleCompoundOxygenSourceExtensions(string fileName, AssetType expected)
    {
        var actual = GameAsset.GetAssetType(fileName);
        Assert.AreEqual(expected, actual);
    }
}
