// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;

namespace Oxygen.Editor.Models;

/// <summary>What a model source holds, from the metadata parse of its analysis (the <c>model</c> of an
/// <c>analyze-sources</c> job): its format, units and axes, counts, scenes and the names of what it produces.</summary>
public sealed record ModelSourceFacts
{
    /// <summary>Gets the file format: <c>gltf</c>, <c>glb</c> or <c>fbx</c>.</summary>
    public required string Format { get; init; }

    /// <summary>Gets the format version the file states, such as <c>2.0</c> or <c>7.4</c>; null when it states none.</summary>
    public string? FormatVersion { get; init; }

    /// <summary>Gets the metres per source unit; null when the file does not state its unit.</summary>
    public double? UnitMeters { get; init; }

    /// <summary>Gets the file's right axis, such as <c>+X</c>, or <c>unknown</c>.</summary>
    public required string Right { get; init; }

    /// <summary>Gets the file's up axis, such as <c>+Z</c>, or <c>unknown</c>.</summary>
    public required string Up { get; init; }

    /// <summary>Gets the file's front axis, or <c>unknown</c>.</summary>
    public required string Front { get; init; }

    /// <summary>Gets whether the file's axes are left-handed; null when they are not a valid basis.</summary>
    public bool? IsLeftHanded { get; init; }

    /// <summary>Gets the file's node count.</summary>
    public int NodeCount { get; init; }

    /// <summary>Gets the file's mesh count.</summary>
    public int MeshCount { get; init; }

    /// <summary>Gets the file's scenes; empty for a format with one root (FBX).</summary>
    public ImmutableArray<ModelSourceScene> Scenes { get; init; } = [];

    /// <summary>Gets the names the import gives the materials it produces.</summary>
    public ImmutableArray<string> Materials { get; init; } = [];

    /// <summary>Gets the names the import gives the textures it produces.</summary>
    public ImmutableArray<string> Textures { get; init; } = [];

    /// <summary>Gets the importer family of the file.</summary>
    public ModelSourceFormats SourceFormat
        => string.Equals(this.Format, "fbx", StringComparison.Ordinal) ? ModelSourceFormats.Fbx : ModelSourceFormats.Gltf;

    /// <summary>Reads the facts of an analysis job's <c>model</c> object, which the source-analysis schema validated.</summary>
    /// <param name="model">The job's <c>model</c> object.</param>
    /// <returns>The facts.</returns>
    public static ModelSourceFacts Read(JsonElement model)
        => new()
        {
            Format = model.GetProperty("format").GetString()!,
            FormatVersion = model.GetProperty("format_version").ValueKind == JsonValueKind.String
                ? model.GetProperty("format_version").GetString() : null,
            UnitMeters = model.GetProperty("unit_meters").ValueKind == JsonValueKind.Number
                ? model.GetProperty("unit_meters").GetDouble() : null,
            Right = model.GetProperty("right").GetString()!,
            Up = model.GetProperty("up").GetString()!,
            Front = model.GetProperty("front").GetString()!,
            IsLeftHanded = model.GetProperty("left_handed").ValueKind is JsonValueKind.True or JsonValueKind.False
                ? model.GetProperty("left_handed").GetBoolean() : null,
            NodeCount = model.GetProperty("node_count").GetInt32(),
            MeshCount = model.GetProperty("mesh_count").GetInt32(),
            Scenes = [.. model.GetProperty("scenes").EnumerateArray().Select(static scene => new ModelSourceScene(
                scene.GetProperty("index").GetInt32(),
                scene.GetProperty("name").GetString()!,
                scene.GetProperty("selector").GetString()!,
                scene.GetProperty("is_default").GetBoolean(),
                scene.GetProperty("root_count").GetInt32(),
                scene.GetProperty("node_count").GetInt32()))],
            Materials = [.. model.GetProperty("materials").EnumerateArray().Select(static name => name.GetString()!)],
            Textures = [.. model.GetProperty("textures").EnumerateArray().Select(static name => name.GetString()!)],
        };
}
