// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using System.Runtime.CompilerServices;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World;

/// <summary>
/// Checks a scene property's value against the range the scene schema generates for it, the one place a single-property
/// rule is decided and described.
/// </summary>
/// <remarks>
/// The scene model, loading and validation reject every value that is not <see cref="RangeCheck.InRange"/>, and every
/// enumeration value that is not a declared member. Only a commit of values from outside the scene, an inspector edit,
/// goes through <see cref="Commit"/> first, which clamps a value where the property's range declares
/// <see cref="RangeCommit.Clamp"/>; any other value outside its range reaches validation unchanged and is rejected.
/// </remarks>
public static class PropertyChecks
{
    /// <summary>Gets the range of a float property.</summary>
    /// <param name="property">The property.</param>
    /// <returns>The range.</returns>
    /// <exception cref="ArgumentException">The property has no float range.</exception>
    public static PropertyRange<float> RangeOf(PropertyId<float> property)
    {
        ArgumentNullException.ThrowIfNull(property);
        return RangeOf<float>(property.Id);
    }

    /// <summary>Gets the range of an integer property.</summary>
    /// <param name="property">The property.</param>
    /// <returns>The range.</returns>
    /// <exception cref="ArgumentException">The property has no integer range.</exception>
    public static PropertyRange<int> RangeOf(PropertyId<int> property)
    {
        ArgumentNullException.ThrowIfNull(property);
        return RangeOf<int>(property.Id);
    }

    /// <summary>Gets the range of a vector property, which bounds each component.</summary>
    /// <param name="property">The property.</param>
    /// <returns>The range of each component.</returns>
    /// <exception cref="ArgumentException">The property has no float range.</exception>
    public static PropertyRange<float> RangeOf(PropertyId<Vector3> property)
    {
        ArgumentNullException.ThrowIfNull(property);
        return RangeOf<float>(property.Id);
    }

    /// <summary>Checks a float property.</summary>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <returns>The result of the check.</returns>
    /// <exception cref="ArgumentException">The property has no float range.</exception>
    public static RangeCheck Check(PropertyId<float> property, float value) => RangeOf(property).Check(value);

    /// <summary>
    /// Checks a double-precision number, such as a control's value, as the float the property stores, before it is
    /// narrowed: a finite number no float holds is out of range, not infinite.
    /// </summary>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <returns>The result of the check.</returns>
    /// <exception cref="ArgumentException">The property has no float range.</exception>
    public static RangeCheck Check(PropertyId<float> property, double value)
    {
        var range = RangeOf(property);
        return !double.IsFinite(value) ? RangeCheck.NotFinite
            : Math.Abs(value) > float.MaxValue ? RangeCheck.OutOfRange
            : range.Check((float)value);
    }

    /// <summary>Checks an integer property.</summary>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <returns>The result of the check.</returns>
    /// <exception cref="ArgumentException">The property has no integer range.</exception>
    public static RangeCheck Check(PropertyId<int> property, int value) => RangeOf(property).Check(value);

    /// <summary>Checks a vector property: the range bounds each component.</summary>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <returns>
    /// <see cref="RangeCheck.NotFinite"/> when any component is not finite, otherwise <see cref="RangeCheck.OutOfRange"/>
    /// when any component is out of range.
    /// </returns>
    /// <exception cref="ArgumentException">The property has no float range.</exception>
    public static RangeCheck Check(PropertyId<Vector3> property, Vector3 value)
    {
        var range = RangeOf(property);
        RangeCheck[] checks = [range.Check(value.X), range.Check(value.Y), range.Check(value.Z)];
        return checks.Contains(RangeCheck.NotFinite) ? RangeCheck.NotFinite
            : checks.Contains(RangeCheck.OutOfRange) ? RangeCheck.OutOfRange
            : RangeCheck.InRange;
    }

    /// <summary>Requires a float property's value to be in its range.</summary>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <param name="parameter">The name of the caller's parameter that holds the value.</param>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range; the message names the property.</exception>
    /// <exception cref="ArgumentException">The property has no float range.</exception>
    public static void Require(PropertyId<float> property, float value, [CallerArgumentExpression(nameof(value))] string? parameter = null)
    {
        var check = Check(property, value);
        RequireInRange(property.Id, check, value, parameter);
    }

    /// <summary>Requires an integer property's value to be in its range.</summary>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <param name="parameter">The name of the caller's parameter that holds the value.</param>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range; the message names the property.</exception>
    /// <exception cref="ArgumentException">The property has no integer range.</exception>
    public static void Require(PropertyId<int> property, int value, [CallerArgumentExpression(nameof(value))] string? parameter = null)
    {
        var check = Check(property, value);
        RequireInRange(property.Id, check, value, parameter);
    }

    /// <summary>Requires each component of a vector property's value to be in its range.</summary>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <param name="parameter">The name of the caller's parameter that holds the value.</param>
    /// <exception cref="ArgumentOutOfRangeException">A component is outside the range; the message names the property.</exception>
    /// <exception cref="ArgumentException">The property has no float range.</exception>
    public static void Require(PropertyId<Vector3> property, Vector3 value, [CallerArgumentExpression(nameof(value))] string? parameter = null)
    {
        var check = Check(property, value);
        RequireInRange(property.Id, check, value, parameter);
    }

    /// <summary>Requires an enumeration property's value to be a declared member.</summary>
    /// <typeparam name="T">The enumeration.</typeparam>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <param name="parameter">The name of the caller's parameter that holds the value.</param>
    /// <exception cref="ArgumentOutOfRangeException">The value is not a declared member; the message names the property.</exception>
    public static void Require<T>(PropertyId<T> property, T value, [CallerArgumentExpression(nameof(value))] string? parameter = null)
        where T : struct, Enum
    {
        ArgumentNullException.ThrowIfNull(property);
        if (!Enum.IsDefined(value))
        {
            throw new ArgumentOutOfRangeException(parameter, value, DescribeUndefined(property.Id.Qualified()));
        }
    }

    /// <summary>
    /// Gets the value a commit of outside values, an inspector edit, writes for a value: a finite value outside the
    /// property's range is clamped into it where the range declares <see cref="RangeCommit.Clamp"/>.
    /// </summary>
    /// <param name="property">The property.</param>
    /// <param name="value">The edited value, of the property's value type.</param>
    /// <returns>
    /// The clamped value, or <paramref name="value"/> itself when it is in range, the range rejects, or the property has
    /// no range; validation then rejects a value still out of range.
    /// </returns>
    public static object? Commit(PropertyId property, object? value)
    {
        ArgumentNullException.ThrowIfNull(property);
        return value switch
        {
            float number when SceneSchema.RangeOf<float>(property) is { } range && range.TryCommit(number, out var committed) => committed,
            int number when SceneSchema.RangeOf<int>(property) is { } range && range.TryCommit(number, out var committed) => committed,
            Vector3 vector when SceneSchema.RangeOf<float>(property) is { } range
                && range.TryCommit(vector.X, out var x) && range.TryCommit(vector.Y, out var y) && range.TryCommit(vector.Z, out var z) => new Vector3(x, y, z),
            _ => value,
        };
    }

    /// <summary>Describes why a check failed, naming the property by its qualified id.</summary>
    /// <param name="property">The property.</param>
    /// <param name="check">The failed check.</param>
    /// <returns>The reason, with the property's valid range.</returns>
    /// <exception cref="ArgumentException">The check is <see cref="RangeCheck.InRange"/>, or the property has no range.</exception>
    public static string Describe(PropertyId property, RangeCheck check)
    {
        ArgumentNullException.ThrowIfNull(property);
        return Describe(property, check, property.Qualified());
    }

    /// <summary>Describes why a check failed, naming the property as a view shows it.</summary>
    /// <param name="property">The property.</param>
    /// <param name="check">The failed check.</param>
    /// <param name="label">The property's name in the message.</param>
    /// <returns>The reason, with the property's valid range.</returns>
    /// <exception cref="ArgumentException">The check is <see cref="RangeCheck.InRange"/>, or the property has no range.</exception>
    public static string Describe(PropertyId property, RangeCheck check, string label)
    {
        ArgumentNullException.ThrowIfNull(property);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        return check switch
        {
            RangeCheck.NotFinite => $"{label} must be a finite number.",
            RangeCheck.OutOfRange => Bounds(property) is { } bounds
                ? $"{label} must be {bounds}."
                : $"{label} is beyond the values a single-precision number holds.",
            _ => throw new ArgumentException("A value in range has nothing to describe.", nameof(check)),
        };
    }

    /// <summary>Describes an enumeration value that is not a declared member.</summary>
    /// <param name="label">The property's name in the message.</param>
    /// <returns>The reason.</returns>
    public static string DescribeUndefined(string label)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        return $"{label} is not one of its defined values.";
    }

    private static void RequireInRange<T>(PropertyId property, RangeCheck check, T value, string? parameter)
    {
        if (check != RangeCheck.InRange)
        {
            throw new ArgumentOutOfRangeException(parameter, value, Describe(property, check));
        }
    }

    private static PropertyRange<T> RangeOf<T>(PropertyId property)
        where T : struct, INumber<T>
        => SceneSchema.RangeOf<T>(property)
            ?? throw new ArgumentException($"{property.Qualified()} has no {typeof(T).Name} range in the scene schema.", nameof(property));

    // The bounds in words, or null for a range with none: such a range rejects only what no float holds.
    private static string? Bounds(PropertyId property)
        => SceneSchema.RangeOf<float>(property) is { } single ? Bounds(single)
            : SceneSchema.RangeOf<int>(property) is { } integer ? Bounds(integer)
            : throw new ArgumentException($"{property.Qualified()} has no range in the scene schema.", nameof(property));

    private static string? Bounds<T>(PropertyRange<T> range)
        where T : struct, INumber<T>
    {
        var bounds = (range.Min, range.Max) switch
        {
            ({ } min, { } max) => $"between {Format(min)} and {Format(max)}",
            ({ } min, null) => range.MinExclusive ? $"greater than {Format(min)}" : $"{Format(min)} or greater",
            (null, { } max) => range.MaxExclusive ? $"less than {Format(max)}" : $"{Format(max)} or less",
            _ => null,
        };
        return (bounds, range.NonZero) switch
        {
            (null, true) => "non-zero",
            (null, false) => null,
            (_, true) => $"{bounds} and non-zero",
            _ => bounds,
        };
    }

    private static string Format<T>(T value)
        where T : struct, INumber<T>
        => string.Create(CultureInfo.InvariantCulture, $"{value:0.####}");
}
