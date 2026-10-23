// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// The validators of the descriptors: one check per property from the range the scene schema generates for it, reported
/// as <see cref="SceneDiagnosticCodes.PropertyValueNotFinite"/>, <see cref="SceneDiagnosticCodes.PropertyValueOutOfRange"/>
/// or <see cref="SceneDiagnosticCodes.PropertyValueUndefined"/> with the message <see cref="PropertyChecks"/> gives.
/// </summary>
/// <remarks>
/// A validator rejects every value outside the range, whatever the range's commit policy: an edit refuses, never
/// clamps. A commit of outside values, an inspector edit, clamps before it submits the edit, through
/// <see cref="PropertyChecks.Commit"/>, so a value it clamps arrives here in range.
/// </remarks>
internal static class PropertyValidators
{
    /// <summary>Gets the validator of a float property.</summary>
    /// <param name="property">The property.</param>
    /// <param name="label">The name shown in messages.</param>
    /// <returns>The validator.</returns>
    public static Func<float, ValidationResult> Of(PropertyId<float> property, string label)
    {
        var range = PropertyChecks.RangeOf(property);
        return value => Result(property.Id, range.Check(value), label);
    }

    /// <summary>Gets the validator of an integer property.</summary>
    /// <param name="property">The property.</param>
    /// <param name="label">The name shown in messages.</param>
    /// <returns>The validator.</returns>
    public static Func<int, ValidationResult> Of(PropertyId<int> property, string label)
    {
        var range = PropertyChecks.RangeOf(property);
        return value => Result(property.Id, range.Check(value), label);
    }

    /// <summary>Gets the validator of a vector property, which checks each component.</summary>
    /// <param name="property">The property.</param>
    /// <param name="label">The name shown in messages.</param>
    /// <returns>The validator.</returns>
    public static Func<Vector3, ValidationResult> Of(PropertyId<Vector3> property, string label)
    {
        // A property the schema gives no range fails here, when the descriptors are built, not at the first edit.
        _ = PropertyChecks.RangeOf(property);
        return value => Result(property.Id, PropertyChecks.Check(property, value), label);
    }

    /// <summary>Gets the validator of an enumeration property: the value must be a declared member.</summary>
    /// <typeparam name="T">The enumeration.</typeparam>
    /// <param name="label">The name shown in messages.</param>
    /// <returns>The validator.</returns>
    public static Func<T, ValidationResult> Defined<T>(string label)
        where T : struct, Enum
        => value => Enum.IsDefined(value)
            ? ValidationResult.Ok
            : ValidationResult.Fail(SceneDiagnosticCodes.PropertyValueUndefined, PropertyChecks.DescribeUndefined(label));

    private static ValidationResult Result(PropertyId property, RangeCheck check, string label)
        => check switch
        {
            RangeCheck.InRange => ValidationResult.Ok,
            RangeCheck.NotFinite => ValidationResult.Fail(SceneDiagnosticCodes.PropertyValueNotFinite, PropertyChecks.Describe(property, check, label)),
            _ => ValidationResult.Fail(SceneDiagnosticCodes.PropertyValueOutOfRange, PropertyChecks.Describe(property, check, label)),
        };
}
