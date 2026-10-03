// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Inspector.Presentation;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Inspector;

[TestClass]
public sealed class InspectorSearchModelTests
{
    [TestMethod]
    public void Catalog_RegistersEveryExistingScenePropertyAndAllFortyTwoCards()
    {
        var model = EnvironmentFieldCatalog.Create();
        _ = model.Fields.Should().HaveCount(42);
        _ = model.Fields.Values.Select(field => field.Property).Should().Contain(
            SceneDocumentCommandService.SceneEnvironment.ById.Keys);
        _ = model.Fields.Values.Select(field => field.Key).Should().OnlyHaveUniqueItems();
    }

    [TestMethod]
    [DataRow("ground_albedo", "GroundAlbedo")]
    [DataRow("Aerial Start", "AerialPerspectiveStartDepthMeters")]
    [DataRow("auto/exposure/min/ev", "AutoExposureMinEv")]
    [DataRow("disk luminance", "Sources")]
    [DataRow("atmosphere_disk_luminance_scale_rgb", "Sources")]
    [DataRow("use_per_pixel_atmosphere_transmittance", "Sources")]
    [DataRow("metered EV100 compensation", "AutoExposureCompensationCurve")]
    public void Search_UsesStableAliasesAndAndMatching(string query, string expected)
    {
        var model = EnvironmentFieldCatalog.Create();
        model.Update(query, InspectorPropertyScope.All, ExposureMode.Manual, MeteringMode.Average, ToneMappingMode.AcesFitted);
        _ = model.Fields[expected].IsVisible.Should().BeTrue();
        _ = model.HasNoMatches.Should().BeFalse();
        model.Update($"{query} unknown-field", InspectorPropertyScope.All, ExposureMode.Manual, MeteringMode.Average, ToneMappingMode.AcesFitted);
        _ = model.HasNoMatches.Should().BeTrue();
    }

    [TestMethod]
    public void Search_TracksApplicabilityAcrossModeChangesWithoutRebuildingMetadata()
    {
        var model = EnvironmentFieldCatalog.Create();
        var field = model.Fields["AutoExposureSpotMeterRadius"];
        model.Update("spot radius", InspectorPropertyScope.All, ExposureMode.Manual, MeteringMode.Average, ToneMappingMode.AcesFitted);
        _ = field.IsVisible.Should().BeTrue();
        _ = field.ApplicabilityText.Should().Contain("Auto exposure mode with Spot metering");
        model.Update("spot radius", InspectorPropertyScope.All, ExposureMode.Auto, MeteringMode.Spot, ToneMappingMode.AcesFitted);
        _ = field.IsVisible.Should().BeTrue();
        _ = field.HasApplicabilityText.Should().BeFalse();
        model.Update(string.Empty, InspectorPropertyScope.All, ExposureMode.Auto, MeteringMode.Average, ToneMappingMode.None);
        _ = field.IsVisible.Should().BeFalse();
        _ = model.Fields["DisplayGamma"].IsVisible.Should().BeTrue();
        _ = model.Fields["ExposureKey"].IsVisible.Should().BeTrue();
    }

    [TestMethod]
    public void Search_RestoresStoredExpansionAfterQueryAndScopeChanges()
    {
        var model = EnvironmentFieldCatalog.Create();
        model.RecordExpansion("PlanetGround", false);
        model.RecordExpansion("Exposure", true);
        model.Update("ground_albedo", InspectorPropertyScope.All, ExposureMode.Manual, MeteringMode.Average, ToneMappingMode.AcesFitted);
        _ = model.IsExpanded("PlanetGround").Should().BeTrue();
        model.RecordExpansion("PlanetGround", true);
        model.Update("ground_albedo", InspectorPropertyScope.PostProcessing, ExposureMode.Manual, MeteringMode.Average, ToneMappingMode.AcesFitted);
        _ = model.HasNoMatches.Should().BeTrue();
        model.Update(string.Empty, InspectorPropertyScope.All, ExposureMode.Manual, MeteringMode.Average, ToneMappingMode.AcesFitted);
        _ = model.IsExpanded("PlanetGround").Should().BeFalse();
        _ = model.IsExpanded("Exposure").Should().BeTrue();
    }

    [TestMethod]
    public void Search_HasNoDependenceOnCurrentEnumSelectionOrDiagnosticText()
    {
        var model = EnvironmentFieldCatalog.Create();
        model.Update("CenterWeighted", InspectorPropertyScope.All, ExposureMode.Manual, MeteringMode.Average, ToneMappingMode.None);
        _ = model.Fields["AutoExposureMeteringMode"].IsVisible.Should().BeTrue();
        model.Update("stored value", InspectorPropertyScope.All, ExposureMode.Manual, MeteringMode.Spot, ToneMappingMode.None);
        _ = model.HasNoMatches.Should().BeTrue();
    }

    [TestMethod]
    public void Search_PreservesBrowsingScopeKeywordsIndependentlyOfRealization()
    {
        var model = EnvironmentFieldCatalog.Create();
        model.Update("post processing", InspectorPropertyScope.All, ExposureMode.Manual, MeteringMode.Average, ToneMappingMode.None);
        _ = model.Fields.Values.Where(entry => entry.IsVisible).Should().HaveCount(27);
        _ = model.Fields.Values.Where(entry => entry.IsVisible).Should().OnlyContain(entry => entry.Scope == InspectorPropertyScope.PostProcessing);
        model.Update("environment", InspectorPropertyScope.All, ExposureMode.Manual, MeteringMode.Average, ToneMappingMode.None);
        _ = model.Fields.Values.Where(entry => entry.IsVisible).Should().HaveCount(15);
    }

    [TestMethod]
    public void Catalog_SourceRolesHaveDistinctPresentationKeysForSharedCanonicalProperties()
    {
        var fields = EnvironmentFieldCatalog.SourceFields;
        _ = fields.Should().HaveCount(10);
        _ = fields.Select(entry => (entry.Role, entry.Key)).Should().OnlyHaveUniqueItems();
        foreach (var role in new[] { AtmosphereLightSlot.Primary, AtmosphereLightSlot.Secondary })
        {
            var direction = fields.Where(entry => entry.Role == role && entry.Key.StartsWith("Sun", StringComparison.Ordinal)).ToArray();
            _ = direction.Should().HaveCount(2);
            _ = direction.Should().OnlyContain(entry => entry.Properties.Length == 3);
            _ = direction[0].Properties.Should().Equal(direction[1].Properties);
            _ = fields.Single(entry => entry.Role == role && string.Equals(entry.Key, "AngularSizeRadians", StringComparison.Ordinal))
                .Properties.Should().Equal(SceneDocumentCommandService.DirectionalLight.AngularSizeRadians.Id);
        }
    }
}
