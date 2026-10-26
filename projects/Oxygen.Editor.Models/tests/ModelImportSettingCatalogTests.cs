// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Models.TestSupport;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// The settings a person chooses are keys of the import manifest's model job, with the schema's values, ranges and
/// defaults, so the editor and the ImportTool import a file the same way (model editor LLD section 5.3).
/// </summary>
[TestClass]
public sealed class ModelImportSettingCatalogTests
{
    public static IEnumerable<object[]> Settings => ModelImportSettingCatalog.All.Select(static setting => new object[] { setting.Key });

    [TestMethod]
    [DynamicData(nameof(Settings))]
    public void EverySettingMatchesTheInstalledManifestSchema(string key)
    {
        var setting = ModelImportSettingCatalog.All.Single(candidate => string.Equals(candidate.Key, key, StringComparison.Ordinal));
        var schema = InstalledSchemas.JobKey(InstalledSchemas.ImportManifest(), key);

        if (schema["enum"] is { } choices)
        {
            _ = setting.Choices.Should().BeEquivalentTo(choices.AsArray().Select(static choice => choice!.GetValue<string>()));
        }

        if (schema["minimum"] is { } minimum)
        {
            _ = setting.Minimum.Should().Be(minimum.GetValue<double>());
            _ = setting.IsMinimumExclusive.Should().BeFalse();
        }

        if (schema["exclusiveMinimum"] is { } exclusive)
        {
            _ = setting.Minimum.Should().Be(exclusive.GetValue<double>());
            _ = setting.IsMinimumExclusive.Should().BeTrue();
        }

        if (schema["maximum"] is { } maximum)
        {
            _ = setting.Maximum.Should().Be(maximum.GetValue<double>());
        }

        if (schema["default"] is { } stated)
        {
            _ = JsonSerializer.SerializeToElement(setting.Default).GetRawText().Should().Be(stated.ToJsonString(), "the panel's default is the importer's");
        }
    }

    [TestMethod]
    public void FormatSpecificSettingsNameTheirFormat()
    {
        _ = ModelImportSettingCatalog.UpAxis.Formats.Should().Be(ModelSourceFormats.Fbx, "glTF is Y up by specification");
        _ = ModelImportSettingCatalog.OmittedLightRange.Formats.Should().Be(ModelSourceFormats.Gltf);
        _ = ModelImportSettingCatalog.GltfScene.Formats.Should().Be(ModelSourceFormats.Gltf);
        _ = ModelImportSettingCatalog.All.Should().OnlyHaveUniqueItems(static setting => setting.Key);
    }

    [TestMethod]
    public void TheDefaultsOfANewModelAreTheCatalogDefaults()
    {
        var defaults = ModelImportSettings.DefaultSettings("Car");

        foreach (var property in defaults.EnumerateObject().Where(static property => property.Name != ModelImportSettingCatalog.NameKey))
        {
            var setting = ModelImportSettingCatalog.All.Single(candidate => string.Equals(candidate.Key, property.Name, StringComparison.Ordinal));
            _ = setting.ReadFrom(defaults).Should().Be(setting.Default, "a new model's explicit settings equal the importer's defaults");
        }
    }
}
