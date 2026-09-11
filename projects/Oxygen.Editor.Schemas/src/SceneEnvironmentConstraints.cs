// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Globalization;
using System.IO;
using System.Text.Json;

namespace Oxygen.Editor.Schemas;

/// <summary>Shared environment bounds read from the shipped native scene schema.</summary>
public static class SceneEnvironmentConstraints
{
    /// <summary>Identifies the editor property used by validation and property navigation.</summary>
    public const string AerialStartPropertyPath = "/sky_atmosphere/aerial_perspective_start_depth_meters";

    private static readonly Lazy<float> MinimumAerialStart = new(ReadAerialStartMinimum);

    /// <summary>Validates Aerial Start without changing the authored value.</summary>
    /// <param name="value">The authored distance in meters.</param>
    /// <returns>The native schema's acceptance or a readable field error.</returns>
    public static ValidationResult ValidateAerialStart(float value)
    {
        if (!float.IsFinite(value))
        {
            return ValidationResult.Fail("SCHEMA_NUMERIC_RANGE", "Aerial Start must be a finite number.");
        }

        var minimum = MinimumAerialStart.Value;
        return value >= minimum ? ValidationResult.Ok : ValidationResult.Fail(
            "SCHEMA_NUMERIC_RANGE",
            string.Create(CultureInfo.CurrentCulture, $"Aerial Start must be {minimum} m or greater."));
    }

    private static float ReadAerialStartMinimum()
    {
        var assemblyDirectory = Path.GetDirectoryName(typeof(EditorSchemaCatalog).Assembly.Location) ?? AppContext.BaseDirectory;
        using var stream = File.OpenRead(Path.Combine(assemblyDirectory, "Schemas", "oxygen.scene-descriptor.schema.json"));
        using var schema = JsonDocument.Parse(stream);
        return schema.RootElement.GetProperty("definitions")
            .GetProperty("sky_atmosphere_environment")
            .GetProperty("properties")
            .GetProperty("aerial_perspective_start_depth_m")
            .GetProperty("minimum").GetSingle();
    }
}
