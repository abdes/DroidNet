// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Nodes;
using AwesomeAssertions;

namespace Oxygen.Managed.Core.Tests;

/// <summary>Checks that a reference field selects the strings at its path, and only those.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(AssetReferenceField))]
public sealed class AssetReferenceFieldTests
{
    private static readonly JsonNode Scene = JsonNode.Parse("""
        { "Name": "/Content/A.omat",
          "RootNodes": [
            { "Name": "/Content/B.omat", "Components": [ { "GeometryUri": "asset:///Content/Root.ogeo" } ],
              "Children": [ { "Components": [ { "GeometryUri": "asset:///Content/Child.ogeo", "Count": 3 } ],
                "Children": [ { "Components": [ { "GeometryUri": "asset:///Content/Grandchild.ogeo" } ] } ] } ] } ],
          "References": { "Scripts": [ "asset:///Content/S.oscript", 4 ] },
          "Overrides": { "speed": { "Type": "string", "Value": "/Content/C.omat" }, "target": { "Type": "asset", "Value": "asset:///Content/D.omat" } } }
        """)!;

    [TestMethod]
    public void Select_AnyDepth_MatchesTheFieldAtEveryLevel()
        => _ = Values(new AssetReferenceField("RootNodes/**/Components/*/GeometryUri"))
            .Should().Equal("asset:///Content/Root.ogeo", "asset:///Content/Child.ogeo", "asset:///Content/Grandchild.ogeo");

    [TestMethod]
    public void Select_ArrayElements_TakesOnlyStrings()
        => _ = Values(new AssetReferenceField("References/Scripts/*")).Should().Equal("asset:///Content/S.oscript");

    [TestMethod]
    public void Select_OtherFields_AreNotReferences()
        => _ = Values(new AssetReferenceField("RootNodes/*/Components/*/Count")).Should().BeEmpty("a field holds a reference only as a string");

    [TestMethod]
    public void Select_Condition_TakesTheFieldOnlyWhenItsObjectHoldsTheValue()
        => _ = Values(new AssetReferenceField("Overrides/*/Value") { WhenProperty = "Type", WhenValue = "asset" })
            .Should().ContainSingle().Which.Should().Be("asset:///Content/D.omat", "a string value shaped like a path is still text");

    [TestMethod]
    public void Select_MissingPath_FindsNothing()
        => _ = Values(new AssetReferenceField("Environment/SkySphere/Cubemap")).Should().BeEmpty();

    private static string[] Values(AssetReferenceField field) => [.. field.Select(Scene).Select(static value => value.GetValue<string>())];
}
