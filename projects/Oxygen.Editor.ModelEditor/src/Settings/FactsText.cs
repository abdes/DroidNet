// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>Words a model source's analysis facts for the import dialog, such as "FBX 7.4 · centimetres, Z up,
/// right-handed · 14 nodes · 9 meshes · 6 materials · 11 textures".</summary>
public static class FactsText
{
    /// <summary>Gets the facts line: format and version, units and axes, and the counts.</summary>
    /// <param name="facts">A source's facts.</param>
    /// <returns>The line.</returns>
    public static string Line(ModelSourceFacts facts)
    {
        ArgumentNullException.ThrowIfNull(facts);
        var format = facts.Format.ToUpperInvariant() switch
        {
            "GLTF" => "glTF",
            "GLB" => "GLB",
            var other => other,
        };
        List<string> parts =
        [
            facts.FormatVersion is { } version ? $"{format} {version}" : format,
            string.Join(", ", UnitParts(facts)),
        ];
        if (facts.Scenes.Length > 1)
        {
            parts.Add(Count(facts.Scenes.Length, "scene"));
        }

        parts.Add(Count(facts.NodeCount, "node"));
        parts.Add(Count(facts.MeshCount, "mesh", "meshes"));
        parts.Add(Count(facts.Materials.Length, "material"));
        parts.Add(Count(facts.Textures.Length, "texture"));
        return string.Join(" · ", parts);
    }

    private static IEnumerable<string> UnitParts(ModelSourceFacts facts)
    {
        yield return UnitName(facts.UnitMeters);
        yield return string.Equals(facts.Up, "unknown", StringComparison.Ordinal) ? "up axis not stated" : facts.Up.TrimStart('+') + " up";
        if (facts.IsLeftHanded is { } left)
        {
            yield return left ? "left-handed" : "right-handed";
        }
    }

    private static string UnitName(double? meters)
        => meters switch
        {
            null => "unit not stated",
            1 => "metres",
            0.01 => "centimetres",
            0.001 => "millimetres",
            0.0254 => "inches",
            0.3048 => "feet",
            { } other => string.Create(CultureInfo.CurrentCulture, $"{other:G4} m per unit"),
        };

    private static string Count(int count, string one, string? many = null)
        => string.Create(CultureInfo.CurrentCulture, $"{count} {(count == 1 ? one : many ?? one + "s")}");
}
