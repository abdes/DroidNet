// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// A setting reads and writes one key of a model's settings and keeps every other key, so settings the panel does not
/// choose (content flags, per-texture overrides) survive an edit.
/// </summary>
[TestClass]
public sealed class ModelImportSettingTests
{
    private static readonly JsonElement Settings = JsonSerializer.SerializeToElement(new Dictionary<string, object>(StringComparer.Ordinal)
    {
        ["name"] = "Car",
        ["unit_policy"] = "custom",
        ["unit_scale"] = 0.5,
        ["texture_overrides"] = new Dictionary<string, object>(StringComparer.Ordinal) { ["Paint_N"] = new Dictionary<string, object>(StringComparer.Ordinal) { ["max_size"] = 512 } },
    });

    [TestMethod]
    public void ReadFrom_ReturnsTheValueOrNullWhenUnset()
    {
        _ = ModelImportSettingCatalog.UnitPolicy.ReadFrom(Settings).Should().Be("custom");
        _ = ModelImportSettingCatalog.UnitScale.ReadFrom(Settings).Should().Be(0.5);
        _ = ModelImportSettingCatalog.ImportLights.ReadFrom(Settings).Should().BeNull();
    }

    [TestMethod]
    public void WriteTo_SetsOrRemovesTheKeyAndKeepsTheOthers()
    {
        var set = ModelImportSettingCatalog.ImportLights.WriteTo(Settings, value: false);
        var removed = ModelImportSettingCatalog.UnitScale.WriteTo(set, value: null);

        _ = ModelImportSettingCatalog.ImportLights.ReadFrom(removed).Should().Be(expected: false);
        _ = removed.TryGetProperty("unit_scale", out _).Should().BeFalse();
        _ = removed.GetProperty("texture_overrides").GetRawText().Should().Be(Settings.GetProperty("texture_overrides").GetRawText());
        _ = removed.GetProperty("name").GetString().Should().Be("Car");
    }

    [TestMethod]
    [DataRow(181d)]
    [DataRow(-1d)]
    [DataRow(double.NaN)]
    public void WriteTo_RefusesAValueOutsideTheRange(double angle)
    {
        var write = () => ModelImportSettingCatalog.SmoothingAngle.WriteTo(Settings, angle);

        _ = write.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void Accepts_HonorsChoicesKindsAndExclusiveMinimums()
    {
        _ = ModelImportSettingCatalog.UpAxis.Accepts("z").Should().BeTrue();
        _ = ModelImportSettingCatalog.UpAxis.Accepts("x").Should().BeFalse();
        _ = ModelImportSettingCatalog.ImportCameras.Accepts("true").Should().BeFalse();
        _ = ModelImportSettingCatalog.OmittedLightRange.Accepts(0d).Should().BeFalse();
        _ = ModelImportSettingCatalog.LightIntensityScale.Accepts(0d).Should().BeTrue();
        _ = ModelImportSettingCatalog.MaxTextureSize.Accepts(2048).Should().BeTrue();
    }

    [TestMethod]
    public void ReadFrom_RefusesAValueOfAnotherKind()
    {
        var wrong = JsonSerializer.SerializeToElement(new Dictionary<string, object>(StringComparer.Ordinal) { ["import_lights"] = "yes" });

        var read = () => ModelImportSettingCatalog.ImportLights.ReadFrom(wrong);

        _ = read.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void MaterialRemap_MapsAndImportsAgain()
    {
        var remapped = ModelMaterialRemap.WriteTo(Settings, "Paint", "/Content/Materials/CarPaint.omat");
        var imported = ModelMaterialRemap.WriteTo(remapped, "Paint", target: null);

        _ = ModelMaterialRemap.Read(remapped).Should().ContainSingle().Which.Should().Be(new KeyValuePair<string, string>("Paint", "/Content/Materials/CarPaint.omat"));
        _ = imported.TryGetProperty(ModelImportSettingCatalog.MaterialRemapKey, out _).Should().BeFalse("an empty remap leaves no key");
        _ = imported.GetProperty("unit_policy").GetString().Should().Be("custom");
    }
}
