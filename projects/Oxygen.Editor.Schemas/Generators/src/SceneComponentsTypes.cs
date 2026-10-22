// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Schemas.Generators;

/// <summary>The editor types of the schema's value types and enumerations, and the encodings the editor implements.</summary>
internal static class SceneComponentsTypes
{
    /// <summary>The encoding from degrees, as the editor edits a value, to radians, as the engine takes it.</summary>
    public const string DegreesToRadians = "degrees-to-radians";

    private static readonly Dictionary<string, string> Values = new(StringComparer.Ordinal)
    {
        ["bool"] = "bool",
        ["int"] = "int",
        ["float"] = "float",
        ["vec3"] = "global::System.Numerics.Vector3",
        ["asset"] = "global::Oxygen.Managed.Core.AssetPath?",
    };

    private static readonly Dictionary<string, string> Enums = new(StringComparer.Ordinal)
    {
        ["CameraAspectMode"] = "global::Oxygen.Managed.Core.CameraAspectMode",
        ["AtmosphereLightSlot"] = "global::Oxygen.Editor.World.Serialization.AtmosphereLightSlot",
        ["ShadowResolutionHint"] = "global::Oxygen.Editor.World.Serialization.ShadowResolutionHint",
        ["DirectionalCsmSplitMode"] = "global::Oxygen.Editor.World.Serialization.DirectionalCsmSplitMode",
    };

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

    /// <summary>Gets the name of the editor method that implements an encoding.</summary>
    /// <param name="encoding">The schema's encoding name.</param>
    /// <returns>The method name, in PascalCase.</returns>
    public static string EncodingMethod(string encoding)
        => string.Concat(encoding.Split('-').Select(static part => char.ToUpperInvariant(part[0]) + part.Substring(1)));
}
