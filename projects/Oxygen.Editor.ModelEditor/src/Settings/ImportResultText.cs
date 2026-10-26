// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text.Json;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>Says what an import does to a model's size and orientation, from the file's facts and its settings.</summary>
/// <remarks>
/// Oxygen's space is right-handed, Z up, forward −Y, in metres (<c>Oxygen/Core/Constants.h</c>). A glTF file is in metres
/// and Y up by specification. An FBX file states its unit and axes; one that states no unit is read as centimetres, one
/// that states no axes as right-handed Y up, and a left-handed one is mirrored along forward. A chosen up axis replaces
/// the file's.
/// </remarks>
public static class ImportResultText
{
    /// <summary>Describes what the import does to a model.</summary>
    /// <param name="facts">The file's analysis facts.</param>
    /// <param name="settings">The model's import settings.</param>
    /// <returns>Two short sentences: what happens to the size, then to the orientation.</returns>
    public static string Describe(ModelSourceFacts facts, JsonElement settings)
    {
        ArgumentNullException.ThrowIfNull(facts);
        return Size(facts, settings) + " " + Orientation(facts, settings);
    }

    // Says whether the model keeps its real size in Oxygen's metres, with what one file unit measures there.
    private static string Size(ModelSourceFacts facts, JsonElement settings)
    {
        var policy = ModelImportSettingCatalog.UnitPolicy.ReadFrom(settings) as string ?? (string)ModelImportSettingCatalog.UnitPolicy.Default!;
        if (string.Equals(policy, "custom", StringComparison.Ordinal))
        {
            var scale = ModelImportSettingCatalog.UnitScale.ReadFrom(settings) as double? ?? (double)ModelImportSettingCatalog.UnitScale.Default!;
            return string.Create(CultureInfo.CurrentCulture, $"Made {scale:G6}× its real size, as chosen.");
        }

        if (facts.SourceFormat == ModelSourceFormats.Gltf)
        {
            return "Real size kept: glTF is in metres, as Oxygen is.";
        }

        var stated = facts.UnitMeters;
        if (stated is 1)
        {
            return "Real size kept: the file is in metres, as Oxygen is.";
        }

        if (string.Equals(policy, "preserve", StringComparison.Ordinal))
        {
            if (stated is not { } unit)
            {
                return "Not converted: 1 file unit = 1 m in Oxygen; the file states no unit.";
            }

            var factor = 1 / unit;
            var change = factor > 1
                ? string.Create(CultureInfo.CurrentCulture, $"{factor:G4}× larger")
                : string.Create(CultureInfo.CurrentCulture, $"{unit:G4}× smaller");
            return string.Create(CultureInfo.CurrentCulture, $"Not converted, so {change} than its real size: 1 file unit ({unit:G6} m) = 1 m in Oxygen.");
        }

        return stated is { } meters
            ? string.Create(CultureInfo.CurrentCulture, $"Real size kept: converted from {Plural(meters)} to Oxygen's metres (1 file unit = {meters:G6} m).")
            : "The file states no unit, so it is read as centimetres and converted to Oxygen's metres (1 file unit = 0.01 m).";
    }

    private static string Orientation(ModelSourceFacts facts, JsonElement settings)
    {
        if (facts.SourceFormat == ModelSourceFormats.Gltf)
        {
            return "Turned from Y up to Oxygen's Z up.";
        }

        var chosen = ModelImportSettingCatalog.UpAxis.ReadFrom(settings) as string ?? (string)ModelImportSettingCatalog.UpAxis.Default!;
        if (!string.Equals(chosen, "from-file", StringComparison.Ordinal))
        {
            return string.Equals(chosen, "z", StringComparison.Ordinal)
                ? "Read as Z up, as chosen; Oxygen is Z up too."
                : "Read as Y up, as chosen, and turned to Oxygen's Z up.";
        }

        if (string.Equals(facts.Up, "unknown", StringComparison.Ordinal))
        {
            return "The file states no axes: read as Y up and turned to Oxygen's Z up.";
        }

        var up = facts.Up.TrimStart('+');
        var turn = string.Equals(facts.Up, "+Z", StringComparison.Ordinal)
            ? "Already Z up, as Oxygen is"
            : $"Turned from {up} up to Oxygen's Z up";
        return facts.IsLeftHanded == true ? turn + ", and mirrored along forward to be right-handed." : turn + ".";
    }

    private static string Plural(double meters)
        => meters switch
        {
            0.01 => "centimetres",
            0.001 => "millimetres",
            0.0254 => "inches",
            0.3048 => "feet",
            1000 => "kilometres",
            _ => string.Create(CultureInfo.CurrentCulture, $"units of {meters:G4} m"),
        };
}
