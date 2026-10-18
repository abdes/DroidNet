// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;

namespace Oxygen.Managed.Core.Tests;

/// <summary>Checks the asset identity type: its spellings, its ordinal equality and its path operations.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(AssetPath))]
public sealed class AssetPathTests
{
    [TestMethod]
    public void Create_MountAndRelativePath_JoinsThem()
    {
        var path = AssetPath.Create("Content", "Materials/Wood.omat");

        _ = path.VirtualPath.Should().Be("/Content/Materials/Wood.omat");
        _ = path.Mount.Should().Be("Content");
        _ = path.RelativePath.Should().Be("Materials/Wood.omat");
        _ = path.Name.Should().Be("Wood.omat");
        _ = path.IsMount.Should().BeFalse();
    }

    [TestMethod]
    public void Create_NoRelativePath_IsTheMount()
    {
        _ = AssetPath.Create("Content").Should().Be(AssetPath.Content);
        _ = AssetPath.Create("Content", relativePath: string.Empty).IsMount.Should().BeTrue();
        _ = AssetPath.Create(".cooked").VirtualPath.Should().Be("/.cooked");
        _ = AssetPath.Content.RelativePath.Should().BeEmpty();
        _ = AssetPath.Content.Parent.Should().BeNull();
    }

    [TestMethod]
    public void Create_Backslashes_SeparateSegments()
        => _ = AssetPath.Create("Content", "Materials\\Wood.omat").VirtualPath.Should().Be("/Content/Materials/Wood.omat");

    [TestMethod]
    [DataRow("", "a")]
    [DataRow("Con/tent", "a")]
    [DataRow("1Content", "a")]
    [DataRow("Content", "/Materials")]
    [DataRow("Content", "Materials//Wood.omat")]
    [DataRow("Content", "Materials/./Wood.omat")]
    [DataRow("Content", "Materials/../Wood.omat")]
    [DataRow("Content", "Materials/Wood:1.omat")]
    public void Create_RefusedMountOrSegment_Throws(string mount, string relativePath)
    {
        var act = () => AssetPath.Create(mount, relativePath);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    [DataRow("/Content/Materials/Red.omat")]
    [DataRow("/Content")]
    [DataRow("/Engine/Generated/BasicShapes/Cube")]
    [DataRow("/Content/.cooked/Red.omat")]
    public void Parse_CanonicalPath_KeepsIt(string virtualPath)
        => _ = AssetPath.Parse(virtualPath).VirtualPath.Should().Be(virtualPath);

    [TestMethod]
    [DataRow(null)]
    [DataRow("")]
    [DataRow("Content/Red.omat")]
    [DataRow("/Content/")]
    [DataRow("/Content//Red.omat")]
    [DataRow("/1Content/Red.omat")]
    public void TryParse_RefusedPath_Fails(string? virtualPath)
        => _ = AssetPath.TryParse(virtualPath, out _).Should().BeFalse();

    [TestMethod]
    [DataRow("Red Wood.omat")]
    [DataRow("100%.omat")]
    [DataRow("A#B.omat")]
    [DataRow("A+B;C=D.omat")]
    [DataRow("École.omat")]
    [DataRow("木.omat")]
    public void ToUriString_AnyName_RoundTrips(string name)
    {
        var path = AssetPath.Content.Combine("Materials/" + name);

        var uri = path.ToUriString();

        _ = uri.Should().StartWith("asset:///Content/Materials/");
        _ = uri.Should().NotContain("#").And.NotContain("?").And.NotContain(" ");
        _ = AssetPath.ParseUri(uri).Should().Be(path);
        _ = AssetPath.FromUri(path.ToUri()).Should().Be(path);
    }

    [TestMethod]
    public void ToUriString_EscapesEachSegment()
        => _ = AssetPath.Parse("/Content/Red Wood/A%B.omat").ToUriString().Should().Be("asset:///Content/Red%20Wood/A%25B.omat");

    [TestMethod]
    [DataRow("asset:///Content/Red.omat", "/Content/Red.omat")]
    [DataRow("ASSET:///Content/Red.omat", "/Content/Red.omat")]
    [DataRow("asset:///Content/Red%20Wood.omat", "/Content/Red Wood.omat")]
    [DataRow("asset:///Content/Red Wood.omat", "/Content/Red Wood.omat")]
    [DataRow("asset:///Content", "/Content")]
    public void TryParseUri_AssetUri_NamesItsPath(string uri, string expected)
    {
        _ = AssetPath.TryParseUri(uri, out var path).Should().BeTrue();
        _ = path!.VirtualPath.Should().Be(expected);
    }

    [TestMethod]
    [DataRow(null)]
    [DataRow("")]
    [DataRow("file:///Content/Red.omat")]
    [DataRow("asset://Content/Red.omat")]
    [DataRow("asset:Content/Red.omat")]
    [DataRow("asset:///Content/Red.omat?x=1")]
    [DataRow("asset:///Content/Red.omat#part")]
    [DataRow("asset:///Content/A%2FB.omat")]
    [DataRow("asset:///Content/")]
    [DataRow("asset:///")]
    public void TryParseUri_NotAnAssetUri_Fails(string? uri)
        => _ = AssetPath.TryParseUri(uri, out _).Should().BeFalse();

    [TestMethod]
    public void Equality_IsOrdinal()
    {
        var path = AssetPath.Parse("/Content/Red.omat");

        _ = (path == AssetPath.Parse("/Content/Red.omat")).Should().BeTrue();
        _ = (path == AssetPath.Parse("/Content/red.omat")).Should().BeFalse();
        _ = path.GetHashCode().Should().Be(AssetPath.Parse("/Content/Red.omat").GetHashCode());
        _ = new HashSet<AssetPath> { path, AssetPath.Parse("/Content/red.omat") }.Should().HaveCount(2);
    }

    [TestMethod]
    public void Order_IsOrdinal()
    {
        var upper = AssetPath.Parse("/Content/B.omat");
        var lower = AssetPath.Parse("/Content/a.omat");

        _ = (upper < lower).Should().BeTrue();
        _ = new[] { lower, upper }.Order().Should().Equal(upper, lower);
    }

    [TestMethod]
    public void CaseFolded_CaseOnlySiblings_AreEqualWithEqualHashes()
    {
        var first = AssetPath.Parse("/Content/Paint.omat");
        var second = AssetPath.Parse("/Content/PAINT.omat");

        _ = AssetPath.CaseFolded.Equals(first, second).Should().BeTrue();
        _ = AssetPath.CaseFolded.GetHashCode(first).Should().Be(AssetPath.CaseFolded.GetHashCode(second));
    }

    [TestMethod]
    public void IsSameOrInside_ComparesWholeSegments()
    {
        var folder = AssetPath.Parse("/Content/Materials");

        _ = AssetPath.Parse("/Content/Materials").IsSameOrInside(folder).Should().BeTrue();
        _ = AssetPath.Parse("/Content/Materials/Red.omat").IsSameOrInside(folder).Should().BeTrue();
        _ = AssetPath.Parse("/Content/MaterialsOld/Red.omat").IsSameOrInside(folder).Should().BeFalse();
        _ = AssetPath.Parse("/Content/materials/Red.omat").IsSameOrInside(folder).Should().BeFalse();
        _ = AssetPath.Parse("/Content/Materials/Red.omat").GetPathBelow(AssetPath.Content).Should().Be("Materials/Red.omat");
    }

    [TestMethod]
    public void ParentAndWithName_StayInTheMount()
    {
        var path = AssetPath.Parse("/Content/Materials/Red.omat");

        _ = path.Parent.Should().Be(AssetPath.Parse("/Content/Materials"));
        _ = path.MountRoot.Should().Be(AssetPath.Content);
        _ = path.WithName("Blue.omat").Should().Be(AssetPath.Parse("/Content/Materials/Blue.omat"));
    }

    [TestMethod]
    [DataRow("/Content/M/Red.omat.json", "/Content/M/Red.omat")]
    [DataRow("/Content/M/Red.omat", "/Content/M/Red.omat")]
    [DataRow("/Content/M/data.json", "/Content/M/data.json")]
    [DataRow("/Content/Input/Player.input.json", "/Content/Input/Player.input")]
    public void WithoutAuthoringSuffix_SourcePath_IsItsIdentity(string source, string identity)
        => _ = AssetPath.Parse(source).WithoutAuthoringSuffix().Should().Be(AssetPath.Parse(identity));

    [TestMethod]
    public void Json_StoresTheAssetUri()
    {
        var path = AssetPath.Parse("/Content/Red Wood.omat");
        var map = new Dictionary<AssetPath, AssetPath> { [path] = AssetPath.Content };

        var json = JsonSerializer.Serialize(map);

        _ = json.Should().Be("""{"asset:///Content/Red%20Wood.omat":"asset:///Content"}""");
        _ = JsonSerializer.Deserialize<Dictionary<AssetPath, AssetPath>>(json).Should().Equal(map);
    }

    [TestMethod]
    public void Json_NotAnAssetUri_IsRefused()
    {
        var act = () => JsonSerializer.Deserialize<AssetPath>("\"asset:///Content/Red.omat?x\"");

        _ = act.Should().Throw<JsonException>();
    }
}
