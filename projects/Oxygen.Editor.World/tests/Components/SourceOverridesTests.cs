// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies <see cref="SourceOverrides"/>: only a property the schema lets a source supply is stored, on its component
/// and part kinds, with a value of its type; an absent value is the source's.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SourceOverrides))]
public sealed class SourceOverridesTests
{
    private static readonly PropertyId<AssetPath> Material = SceneSchema.Geometry.Surface.Material;
    private static readonly AssetPath Stone = AssetPath.Parse("/Content/Materials/Stone.omat");
    private static readonly AssetPath Moss = AssetPath.Parse("/Content/Materials/Moss.omat");
    private static readonly PartId Top = Surface("30000000-0000-0000-0000-000000000001");
    private static readonly PartId Side = Surface("30000000-0000-0000-0000-000000000002");
    private static readonly string Revision = new('a', 64);
    private static readonly string NewRevision = new('b', 64);

    private static SourceOverrides Empty => SourceOverrides.For(SceneSchema.Geometry.Kind);

    [TestMethod]
    public void With_StoresTheValueOfThePart()
    {
        var overrides = Empty.With(Material, Top, Revision, Stone);

        _ = overrides.TryGet(Material, Top, out var material).Should().BeTrue();
        _ = material.Should().Be(Stone);
        _ = overrides.TryGet(Material, Side, out _).Should().BeFalse("another part keeps the source's value");
        _ = overrides.Parts[Top].LayoutRevision.Should().Be(Revision);
    }

    [TestMethod]
    public void With_ReplacesTheValueAndTheLayoutRevision()
    {
        var overrides = Empty.With(Material, Top, Revision, Stone).With(Material, Top, NewRevision, Moss);

        _ = overrides.Find(Material.Id, Top).Should().Be(new SourceOverride(Material.Id, Top, NewRevision, Moss));
    }

    [TestMethod]
    public void With_LeavesTheOriginalUnchanged()
    {
        var original = Empty;

        _ = original.With(Material, Top, Revision, Stone);

        _ = original.IsEmpty.Should().BeTrue();
    }

    [TestMethod]
    public void Without_RemovesThePartWithItsLastValue()
    {
        var overrides = Empty.With(Material, Top, Revision, Stone).Without(Material.Id, Top);

        _ = overrides.IsEmpty.Should().BeTrue();
        _ = overrides.Parts.Should().BeEmpty();
    }

    [TestMethod]
    public void Without_AnAbsentOverride_ReturnsTheSameValue()
    {
        var overrides = Empty.With(Material, Top, Revision, Stone);

        _ = overrides.Without(Material.Id, Side).Should().BeSameAs(overrides);
    }

    [TestMethod]
    public void All_ListsTheOverridesByPart()
    {
        var overrides = Empty.With(Material, Side, Revision, Moss).With(Material, Top, Revision, Stone);

        _ = overrides.All.Should().Equal(
            new SourceOverride(Material.Id, Top, Revision, Stone),
            new SourceOverride(Material.Id, Side, Revision, Moss));
        _ = overrides.Assets.Should().Equal(Stone, Moss);
    }

    [TestMethod]
    public void MapAssets_ReplacesTheReferencedAssets()
    {
        var overrides = Empty.With(Material, Top, Revision, Stone).With(Material, Side, Revision, Moss);

        var mapped = overrides.MapAssets(asset => asset == Stone ? Moss : asset);

        _ = mapped.Assets.Should().Equal(Moss, Moss);
        _ = mapped.Parts[Top].LayoutRevision.Should().Be(Revision);
    }

    [TestMethod]
    public void Equals_ComparesTheValues()
    {
        _ = Empty.With(Material, Top, Revision, Stone).Should().Be(Empty.With(Material, Top, Revision, Stone));
        _ = Empty.With(Material, Top, Revision, Stone).Should().NotBe(Empty.With(Material, Top, NewRevision, Stone));
        _ = Empty.Should().NotBe(SourceOverrides.For(SceneSchema.PointLight.Kind));
    }

    [TestMethod]
    public void With_APropertyWithoutASource_IsRefused()
    {
        var act = () => Empty.With(new SourceOverride(SceneSchema.Geometry.GeometryUri.Id, Part: null, LayoutRevision: null, Stone));

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void With_APropertyOfAnotherComponent_IsRefused()
    {
        var act = () => SourceOverrides.For(SceneSchema.PointLight.Kind).With(Material, Top, Revision, Stone);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void With_APartPropertyWithoutItsPart_IsRefused()
    {
        var act = () => Empty.With(new SourceOverride(Material.Id, Part: null, LayoutRevision: null, Stone));

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    [DataRow("section", "30000000-0000-0000-0000-000000000001", DisplayName = "Another part kind")]
    [DataRow("surface", "00000000-0000-0000-0000-000000000000", DisplayName = "An empty part id")]
    public void With_AnInvalidPart_IsRefused(string kind, string id)
    {
        var act = () => Empty.With(Material, new PartId(kind, Guid.Parse(id)), Revision, Stone);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    [DataRow("", DisplayName = "Empty")]
    [DataRow("rev-1", DisplayName = "Not a digest")]
    [DataRow("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", DisplayName = "Uppercase")]
    public void With_AnInvalidLayoutRevision_IsRefused(string revision)
    {
        var act = () => Empty.With(Material, Top, revision, Stone);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void With_AValueOfAnotherType_IsRefused()
    {
        var act = () => Empty.With(new SourceOverride(Material.Id, Top, Revision, "Stone"));

        _ = act.Should().Throw<ArgumentException>();
    }

    private static PartId Surface(string id) => new(SceneSchema.Geometry.Surface.Kind, Guid.Parse(id));
}
