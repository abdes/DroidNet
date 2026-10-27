// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// The model import settings of the selected mesh and texture (model editor 5.3): a geometry's LOD policy and surface
/// flags by anchor, and a texture's overrides of the model's texture settings, each removed again when reset, and all of
/// them valid in a model import record.
/// </summary>
[TestClass]
public sealed class ModelImportDefaultsTests
{
    private static readonly Guid Surface = Guid.Parse("0f6d5c1e-8a40-4c5e-9b8e-3c1f2a6b7d90");

    [TestMethod]
    public void AGeometryAnchorIsFoundByTheCookedGeometrysKey()
    {
        var key = Guid.Parse("1b3c5d7e-9f00-4a1b-8c2d-3e4f5a6b7c8d");
        using var provenance = JsonDocument.Parse($$"""{"schema_version":2,"geometries":[{"geometry_asset_key":"{{key:D}}","source_geometry_anchor":"meshes/Body"}]}""");

        _ = ModelGeometryDefaults.AnchorOf(provenance.RootElement, key).Should().Be("meshes/Body");
        _ = ModelGeometryDefaults.AnchorOf(provenance.RootElement, Guid.NewGuid()).Should().BeNull();
    }

    [TestMethod]
    public void AGeometrysLodPolicyIsSetAndResetByAnchor()
    {
        var policy = new LodPolicy.Distance([5f, 10f], 0.1f);

        var set = ModelGeometryDefaults.WithLodPolicy(Empty(), "meshes/Body", policy);
        var reset = ModelGeometryDefaults.WithLodPolicy(set, "meshes/Body", policy: null);

        _ = ModelGeometryDefaults.LodPolicyOf(set, "meshes/Body").Should().Be(policy);
        _ = ModelGeometryDefaults.LodPolicyOf(set, "meshes/Glass").Should().BeNull();
        _ = reset.TryGetProperty(ModelImportSettingCatalog.GeometryDefaultsKey, out _).Should().BeFalse("an empty map leaves the settings");
    }

    [TestMethod]
    public void ASurfaceFlagIsSetAndResetWithoutTouchingTheOther()
    {
        var visible = ModelGeometryDefaults.WithSurfaceFlag(Empty(), "meshes/Body", Surface, ModelSurfaceFlag.Visible, value: false);
        var both = ModelGeometryDefaults.WithSurfaceFlag(visible, "meshes/Body", Surface, ModelSurfaceFlag.CastsShadows, value: false);
        var reset = ModelGeometryDefaults.WithSurfaceFlag(both, "meshes/Body", Surface, ModelSurfaceFlag.Visible, value: null);

        _ = ModelGeometryDefaults.SurfaceFlagOf(both, "meshes/Body", Surface, ModelSurfaceFlag.Visible).Should().BeFalse();
        _ = ModelGeometryDefaults.SurfaceFlagOf(reset, "meshes/Body", Surface, ModelSurfaceFlag.Visible).Should().BeNull();
        _ = ModelGeometryDefaults.SurfaceFlagOf(reset, "meshes/Body", Surface, ModelSurfaceFlag.CastsShadows).Should().BeFalse();
    }

    [TestMethod]
    public void ATextureOverrideInheritsTheModelsValueUntilSet()
    {
        var scope = ModelImportSettingScope.Texture("Paint_Normal");
        var setting = ModelImportSettingCatalog.FlipNormalGreen;
        var model = setting.WriteTo(Empty(), value: true);

        var overridden = scope.Write(setting, model, value: false);
        var reset = scope.Write(setting, overridden, value: null);

        _ = scope.Read(setting, model).Should().BeNull();
        _ = scope.Inherited(setting, model).Should().Be(true);
        _ = scope.Read(setting, overridden).Should().Be(false);
        _ = setting.ReadFrom(overridden).Should().Be(true, "the model's own value is unchanged");
        _ = reset.TryGetProperty(ModelImportSettingCatalog.TextureOverridesKey, out _).Should().BeFalse();
    }

    [TestMethod]
    public void TheDefaultsAreValidInAModelImportRecord()
    {
        var settings = ModelGeometryDefaults.WithLodPolicy(Empty(), "meshes/Body", new LodPolicy.Fixed(1));
        settings = ModelGeometryDefaults.WithSurfaceFlag(settings, "meshes/Body", Surface, ModelSurfaceFlag.CastsShadows, value: false);
        settings = ModelImportSettingScope.Texture("Paint_Normal").Write(ModelImportSettingCatalog.MaxTextureSize, settings, 1024);
        var path = ModelImportSettings.PathFor(@"C:\Project", Guid.CreateVersion7());

        var record = ModelImportSettings.Create(path, @"C:\Project\Content\Car.glb", "/Content/Car.glb", settings);

        _ = JsonElement.DeepEquals(ModelImportSettings.Parse(path, record.ToBytes()).Settings, settings).Should().BeTrue();
    }

    private static JsonElement Empty() => JsonSerializer.SerializeToElement(new Dictionary<string, object>(StringComparer.Ordinal));
}
