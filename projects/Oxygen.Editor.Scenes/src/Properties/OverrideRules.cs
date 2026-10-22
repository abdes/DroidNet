// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// What an override value must satisfy beyond its type, for each property a source may supply
/// (<see cref="SceneSchema.Overridable"/>): every such property has a rule.
/// </summary>
internal static class OverrideRules
{
    private static readonly FrozenDictionary<PropertyId, Func<object, ValidationResult>> Rules = new Dictionary<PropertyId, Func<object, ValidationResult>>
    {
        [SceneSchema.Geometry.Surface.Material.Id] = static value => Asset((AssetPath)value, AssetFileTypes.Material),
    }.ToFrozenDictionary();

    /// <summary>Gets the properties that have a rule.</summary>
    public static IEnumerable<PropertyId> Properties => Rules.Keys;

    /// <summary>Checks an override value of the property's type.</summary>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <returns>The result, with the diagnostic code of a refusal.</returns>
    /// <exception cref="InvalidOperationException">The property has no rule, a defect of this table.</exception>
    public static ValidationResult Check(PropertyId property, object value)
        => Rules.TryGetValue(property, out var rule) ? rule(value) : throw new InvalidOperationException($"{property} has no override rule.");

    private static ValidationResult Asset(AssetPath asset, string fileType)
        => AssetReferenceRules.Check([asset], fileType) is { } error ? ValidationResult.Fail(SceneEditCodes.AssetReferenceInvalid, error) : ValidationResult.Ok;
}
