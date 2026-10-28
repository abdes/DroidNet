// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Schemas.Generators;

/// <summary>The editor types of the schema's value types and enumerations, and the encodings the editor implements.</summary>
internal static class SceneComponentsTypes
{
    /// <summary>The encoding from degrees, as the editor edits a value, to radians, as the engine takes it.</summary>
    public const string DegreesToRadians = "degrees-to-radians";

    /// <summary>
    /// The encoding of three X-Y-Z Euler angles in degrees as one rotation: a change holds its quaternion, and the
    /// fields receive the angles unchanged.
    /// </summary>
    public const string EulerXyzDegreesToQuat = "euler-xyz-degrees-to-quat";

    private static readonly Dictionary<string, string> Values = new(StringComparer.Ordinal)
    {
        ["bool"] = "bool",
        ["int"] = "int",
        ["float"] = "float",
        ["vec3"] = "global::System.Numerics.Vector3",
        ["asset"] = "global::Oxygen.Managed.Core.AssetPath?",
        ["string"] = "string",
        ["lod-policy"] = "global::Oxygen.Editor.World.Serialization.LodPolicy",
    };

    private static readonly Dictionary<string, string> Enums = new(StringComparer.Ordinal)
    {
        ["CameraAspectMode"] = "global::Oxygen.Managed.Core.CameraAspectMode",
        ["AtmosphereLightSlot"] = "global::Oxygen.Editor.World.Serialization.AtmosphereLightSlot",
        ["ShadowResolutionHint"] = "global::Oxygen.Editor.World.Serialization.ShadowResolutionHint",
        ["DirectionalCsmSplitMode"] = "global::Oxygen.Editor.World.Serialization.DirectionalCsmSplitMode",
        ["VisibilitySource"] = "global::Oxygen.Editor.World.Serialization.VisibilitySource",
        ["ShadowSource"] = "global::Oxygen.Editor.World.Serialization.ShadowSource",
    };

    /// <summary>Gets the names of the schema enumerations that have an editor type.</summary>
    public static IEnumerable<string> EnumNames => Enums.Keys;

    /// <summary>Gets a value indicating whether a property's value type has an editor type.</summary>
    /// <param name="property">The property.</param>
    /// <returns><see langword="true"/> when <see cref="EditorType"/> maps it.</returns>
    public static bool IsKnown(SceneComponentsSchema.PropertyEntry property)
        => string.Equals(property.Type, "enum", StringComparison.Ordinal)
            ? property.Enum is not null && Enums.ContainsKey(property.Enum)
            : Values.ContainsKey(property.Type);

    /// <summary>Gets the fully qualified editor type of a property's value.</summary>
    /// <param name="property">A property whose type <see cref="IsKnown"/> accepts.</param>
    /// <returns>The type name.</returns>
    public static string EditorType(SceneComponentsSchema.PropertyEntry property)
        => string.Equals(property.Type, "enum", StringComparison.Ordinal) ? Enums[property.Enum!] : Values[property.Type];

    /// <summary>Gets the editor type of a property's values, which are never null.</summary>
    /// <param name="property">A property whose type is known.</param>
    /// <returns>The fully qualified type name, without a nullable annotation.</returns>
    public static string ValueType(SceneComponentsSchema.PropertyEntry property) => EditorType(property).TrimEnd('?');

    /// <summary>Gets the fully qualified editor type of an enumeration, or <see langword="null"/> when it has none.</summary>
    /// <param name="enumeration">The schema's enumeration name.</param>
    /// <returns>The type name.</returns>
    public static string? EnumType(string enumeration) => Enums.TryGetValue(enumeration, out var type) ? type : null;

    /// <summary>Gets the editor type that bounds a numeric property: a vector is bounded per component.</summary>
    /// <param name="property">A property of type <see langword="int"/>, <see langword="float"/> or <c>vec3</c>.</param>
    /// <returns>The type name.</returns>
    public static string RangeType(SceneComponentsSchema.PropertyEntry property)
        => string.Equals(property.Type, "int", StringComparison.Ordinal) ? "int" : "float";

    /// <summary>Gets a value indicating whether a property's values are bounded by a range.</summary>
    /// <param name="property">The property.</param>
    /// <returns><see langword="true"/> for <see langword="int"/>, <see langword="float"/> and <c>vec3</c> properties.</returns>
    public static bool IsNumeric(SceneComponentsSchema.PropertyEntry property)
        => property.Type is "int" or "float" or "vec3";

    /// <summary>Gets the PascalCase form of a schema id such as a unit id.</summary>
    /// <param name="id">The kebab-case id.</param>
    /// <returns>The name.</returns>
    public static string Pascal(string id)
        => string.Concat(id.Split('-').Select(static part => char.ToUpperInvariant(part[0]) + part.Substring(1)));

    /// <summary>Gets a value indicating whether the editor implements an encoding.</summary>
    /// <param name="encoding">The schema's encoding name.</param>
    /// <returns><see langword="true"/> for <see cref="DegreesToRadians"/> and <see cref="EulerXyzDegreesToQuat"/>.</returns>
    public static bool IsEncoding(string encoding)
        => encoding is DegreesToRadians or EulerXyzDegreesToQuat;

    /// <summary>Gets the name of the editor method that converts a value before it goes to its fields.</summary>
    /// <param name="encoding">An encoding <see cref="IsEncoding"/> accepts.</param>
    /// <returns>The method name, in PascalCase, or <see langword="null"/> when the fields receive the value unchanged.</returns>
    public static string? FieldEncodingMethod(string encoding)
        => string.Equals(encoding, EulerXyzDegreesToQuat, StringComparison.Ordinal) ? null : Pascal(encoding);
}
