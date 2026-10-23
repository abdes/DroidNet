// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.CodeAnalysis;

namespace Oxygen.Editor.Schemas.Generators;

/// <summary>
/// The engine's scene components schema (<c>Oxygen/Scene/Schema/SceneComponents.yaml</c>), as the generator reads it.
/// </summary>
/// <remarks>
/// Every entry keeps the location of its node in the schema file, so a diagnostic points at the entry it is about.
/// </remarks>
internal sealed class SceneComponentsSchema(
    IReadOnlyList<SceneComponentsSchema.UnitEntry> units,
    IReadOnlyList<SceneComponentsSchema.EnumEntry> enums,
    IReadOnlyList<SceneComponentsSchema.FieldSetEntry> fieldSets,
    IReadOnlyList<SceneComponentsSchema.ComponentEntry> components)
{
    /// <summary>Gets the units, in schema order.</summary>
    public IReadOnlyList<UnitEntry> Units { get; } = units;

    /// <summary>Gets the enumerations, in schema order.</summary>
    public IReadOnlyList<EnumEntry> Enums { get; } = enums;

    /// <summary>Gets the field sets, in schema order.</summary>
    public IReadOnlyList<FieldSetEntry> FieldSets { get; } = fieldSets;

    /// <summary>Gets the components, in schema order.</summary>
    public IReadOnlyList<ComponentEntry> Components { get; } = components;

    /// <summary>A unit that UIs show.</summary>
    /// <param name="id">The unit id.</param>
    /// <param name="abbreviation">The short form shown next to a value.</param>
    /// <param name="name">The full plural name.</param>
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class UnitEntry(string id, string abbreviation, string name, SceneComponentsSchema.Locations locations)
    {
        /// <summary>Gets the unit id.</summary>
        public string Id { get; } = id;

        /// <summary>Gets the short form shown next to a value.</summary>
        public string Abbreviation { get; } = abbreviation;

        /// <summary>Gets the full plural name.</summary>
        public string Name { get; } = name;

        /// <summary>Gets the entry's locations in the schema file.</summary>
        public Locations Locations { get; } = locations;
    }

    /// <summary>An enumeration with the wire value of each member.</summary>
    /// <param name="name">The enumeration name.</param>
    /// <param name="members">The members, in schema order.</param>
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class EnumEntry(string name, IReadOnlyList<MemberEntry> members, SceneComponentsSchema.Locations locations)
    {
        /// <summary>Gets the enumeration name.</summary>
        public string Name { get; } = name;

        /// <summary>Gets the members, in schema order.</summary>
        public IReadOnlyList<MemberEntry> Members { get; } = members;

        /// <summary>Gets the entry's locations in the schema file.</summary>
        public Locations Locations { get; } = locations;
    }

    /// <summary>A member of an enumeration.</summary>
    /// <param name="name">The member name.</param>
    /// <param name="value">The wire value.</param>
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class MemberEntry(string name, int value, SceneComponentsSchema.Locations locations)
    {
        /// <summary>Gets the member name.</summary>
        public string Name { get; } = name;

        /// <summary>Gets the wire value.</summary>
        public int Value { get; } = value;

        /// <summary>Gets the entry's locations in the schema file.</summary>
        public Locations Locations { get; } = locations;
    }

    /// <summary>A named set of component-local field ids.</summary>
    /// <param name="name">The field set name.</param>
    /// <param name="fields">The fields, in schema order.</param>
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class FieldSetEntry(string name, IReadOnlyList<FieldEntry> fields, SceneComponentsSchema.Locations locations)
    {
        /// <summary>Gets the field set name.</summary>
        public string Name { get; } = name;

        /// <summary>Gets the fields, in schema order.</summary>
        public IReadOnlyList<FieldEntry> Fields { get; } = fields;

        /// <summary>Gets the entry's locations in the schema file.</summary>
        public Locations Locations { get; } = locations;
    }

    /// <summary>A component-local field id.</summary>
    /// <param name="name">The field name.</param>
    /// <param name="id">The field id.</param>
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class FieldEntry(string name, int id, SceneComponentsSchema.Locations locations)
    {
        /// <summary>Gets the field name.</summary>
        public string Name { get; } = name;

        /// <summary>Gets the field id.</summary>
        public int Id { get; } = id;

        /// <summary>Gets the entry's locations in the schema file.</summary>
        public Locations Locations { get; } = locations;
    }

    /// <summary>An authored scene node component.</summary>
    /// <param name="name">The component name.</param>
    /// <param name="kind">The component kind.</param>
    /// <param name="isRequired">A value indicating whether every node has the component.</param>
    /// <param name="wire">How the engine takes the component field by field, or <see langword="null"/>.</param>
    /// <param name="properties">The properties, in schema order.</param>
    /// <param name="parts">The parts, in schema order.</param>
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class ComponentEntry(string name, string kind, bool isRequired, WireEntry? wire, IReadOnlyList<PropertyEntry> properties, IReadOnlyList<PartEntry> parts, SceneComponentsSchema.Locations locations)
    {
        /// <summary>Gets the component name.</summary>
        public string Name { get; } = name;

        /// <summary>Gets the component kind.</summary>
        public string Kind { get; } = kind;

        /// <summary>Gets a value indicating whether every node has the component.</summary>
        public bool IsRequired { get; } = isRequired;

        /// <summary>Gets how the engine takes the component field by field, or <see langword="null"/>.</summary>
        public WireEntry? Wire { get; } = wire;

        /// <summary>Gets the properties, in schema order.</summary>
        public IReadOnlyList<PropertyEntry> Properties { get; } = properties;

        /// <summary>Gets the parts, in schema order.</summary>
        public IReadOnlyList<PartEntry> Parts { get; } = parts;

        /// <summary>Gets the entry's locations in the schema file.</summary>
        public Locations Locations { get; } = locations;
    }

    /// <summary>A piece of a component, identified by the asset that defines it, such as a surface of a geometry.</summary>
    /// <param name="name">The part name.</param>
    /// <param name="kind">The part kind.</param>
    /// <param name="properties">The properties, in schema order; each has a source.</param>
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class PartEntry(string name, string kind, IReadOnlyList<PropertyEntry> properties, SceneComponentsSchema.Locations locations)
    {
        /// <summary>Gets the part name.</summary>
        public string Name { get; } = name;

        /// <summary>Gets the part kind.</summary>
        public string Kind { get; } = kind;

        /// <summary>Gets the properties, in schema order.</summary>
        public IReadOnlyList<PropertyEntry> Properties { get; } = properties;

        /// <summary>Gets the entry's locations in the schema file.</summary>
        public Locations Locations { get; } = locations;
    }

    /// <summary>A component's engine id and field set.</summary>
    /// <param name="id">The component id.</param>
    /// <param name="fieldSet">The field set name.</param>
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class WireEntry(int id, string fieldSet, SceneComponentsSchema.Locations locations)
    {
        /// <summary>Gets the component id.</summary>
        public int Id { get; } = id;

        /// <summary>Gets the field set name.</summary>
        public string FieldSet { get; } = fieldSet;

        /// <summary>Gets the entry's locations in the schema file.</summary>
        public Locations Locations { get; } = locations;
    }

    /// <summary>An authored property of a component.</summary>
    /// <param name="name">The property name.</param>
    /// <param name="key">The property key, a JSON pointer.</param>
    /// <param name="type">The value type.</param>
    /// <param name="enumeration">The enumeration of an enum value, or <see langword="null"/>.</param>
    /// <param name="fields">The fields that receive the value, in order.</param>
    /// <param name="encoding">The encoding applied before the value goes to its fields, or <see langword="null"/>.</param>
    /// <param name="sources">What may supply the value; a scene then stores only its overrides of it.</param>
    /// <param name="values">The property's override id, default, range and units.</param>
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class PropertyEntry(string name, string key, string type, string? enumeration, IReadOnlyList<string> fields, string? encoding, IReadOnlyList<string> sources, ValuesEntry values, SceneComponentsSchema.Locations locations)
    {
        /// <summary>Gets the property name.</summary>
        public string Name { get; } = name;

        /// <summary>Gets the property key, a JSON pointer.</summary>
        public string Key { get; } = key;

        /// <summary>Gets the value type.</summary>
        public string Type { get; } = type;

        /// <summary>Gets the enumeration of an enum value, or <see langword="null"/>.</summary>
        public string? Enum { get; } = enumeration;

        /// <summary>Gets the fields that receive the value, in order.</summary>
        public IReadOnlyList<string> Fields { get; } = fields;

        /// <summary>Gets the encoding applied before the value goes to its fields, or <see langword="null"/>.</summary>
        public string? Encoding { get; } = encoding;

        /// <summary>Gets what may supply the value; empty when the scene stores the value itself.</summary>
        public IReadOnlyList<string> Sources { get; } = sources;

        /// <summary>Gets the property's override id, default, range and units.</summary>
        public ValuesEntry Values { get; } = values;

        /// <summary>Gets the entry's locations in the schema file.</summary>
        public Locations Locations { get; } = locations;
    }

    /// <summary>What the schema decides about a property's values.</summary>
    /// <param name="overrideId">The override id of a property with sources, or <see langword="null"/>.</param>
    /// <param name="default">The creation value, or <see langword="null"/> for an asset or string property.</param>
    /// <param name="rootDefault">The member a node flag takes on a new root, or <see langword="null"/>.</param>
    /// <param name="range">The valid values of a numeric property, or <see langword="null"/> when unlimited.</param>
    /// <param name="clamps">A value indicating whether an inspector edit clamps into the range.</param>
    /// <param name="unit">The unit id of the stored value, or <see langword="null"/>.</param>
    /// <param name="displayUnit">The unit id UIs show the value in, or <see langword="null"/> for <paramref name="unit"/>.</param>
    internal sealed class ValuesEntry(int? overrideId, ValueEntry? @default, string? rootDefault, RangeEntry? range, bool clamps, string? unit, string? displayUnit)
    {
        /// <summary>Gets the override id of a property with sources, or <see langword="null"/>.</summary>
        public int? OverrideId { get; } = overrideId;

        /// <summary>Gets the creation value, or <see langword="null"/> for an asset or string property.</summary>
        public ValueEntry? Default { get; } = @default;

        /// <summary>Gets the member a node flag takes on a new root, or <see langword="null"/>.</summary>
        public string? RootDefault { get; } = rootDefault;

        /// <summary>Gets the valid values of a numeric property, or <see langword="null"/> when unlimited.</summary>
        public RangeEntry? Range { get; } = range;

        /// <summary>Gets a value indicating whether an inspector edit clamps into the range.</summary>
        public bool Clamps { get; } = clamps;

        /// <summary>Gets the unit id of the stored value, or <see langword="null"/>.</summary>
        public string? Unit { get; } = unit;

        /// <summary>Gets the unit id UIs show the value in, or <see langword="null"/> for <see cref="Unit"/>.</summary>
        public string? DisplayUnit { get; } = displayUnit;
    }

    /// <summary>A property value: one scalar, the numbers of a vector, or a LOD policy.</summary>
    /// <param name="scalar">A bool, number or enumeration member, as written.</param>
    /// <param name="numbers">The components of a vector.</param>
    /// <param name="policy">A LOD policy.</param>
    internal sealed class ValueEntry(string? scalar, IReadOnlyList<double>? numbers, LodPolicyEntry? policy)
    {
        /// <summary>Gets a bool, number or enumeration member, as written, or <see langword="null"/>.</summary>
        public string? Scalar { get; } = scalar;

        /// <summary>Gets the components of a vector, or <see langword="null"/>.</summary>
        public IReadOnlyList<double>? Numbers { get; } = numbers;

        /// <summary>Gets a LOD policy, or <see langword="null"/>.</summary>
        public LodPolicyEntry? Policy { get; } = policy;
    }

    /// <summary>A LOD policy value.</summary>
    /// <param name="type">The policy type: <c>fixed</c>, <c>distance</c> or <c>screen_space_error</c>.</param>
    /// <param name="lodIndex">The LOD of a fixed policy.</param>
    /// <param name="thresholds">The boundaries of a distance policy.</param>
    /// <param name="hysteresisRatio">The hysteresis of a distance policy.</param>
    /// <param name="enterFinerSse">The enter-finer errors of a screen-space error policy.</param>
    /// <param name="exitCoarserSse">The exit-coarser errors of a screen-space error policy.</param>
    internal sealed class LodPolicyEntry(string type, int lodIndex, IReadOnlyList<double> thresholds, double hysteresisRatio, IReadOnlyList<double> enterFinerSse, IReadOnlyList<double> exitCoarserSse)
    {
        /// <summary>Gets the policy type.</summary>
        public string Type { get; } = type;

        /// <summary>Gets the LOD of a fixed policy.</summary>
        public int LodIndex { get; } = lodIndex;

        /// <summary>Gets the boundaries of a distance policy.</summary>
        public IReadOnlyList<double> Thresholds { get; } = thresholds;

        /// <summary>Gets the hysteresis of a distance policy.</summary>
        public double HysteresisRatio { get; } = hysteresisRatio;

        /// <summary>Gets the enter-finer errors of a screen-space error policy.</summary>
        public IReadOnlyList<double> EnterFinerSse { get; } = enterFinerSse;

        /// <summary>Gets the exit-coarser errors of a screen-space error policy.</summary>
        public IReadOnlyList<double> ExitCoarserSse { get; } = exitCoarserSse;
    }

    /// <summary>The valid values of a numeric property; a <see langword="null"/> bound is unlimited.</summary>
    /// <param name="min">The lower bound, or <see langword="null"/>.</param>
    /// <param name="max">The upper bound, or <see langword="null"/>.</param>
    /// <param name="minExclusive">A value indicating whether the lower bound is invalid.</param>
    /// <param name="maxExclusive">A value indicating whether the upper bound is invalid.</param>
    /// <param name="nonZero">A value indicating whether zero is invalid.</param>
    internal sealed class RangeEntry(double? min, double? max, bool minExclusive, bool maxExclusive, bool nonZero)
    {
        /// <summary>Gets the lower bound, or <see langword="null"/>.</summary>
        public double? Min { get; } = min;

        /// <summary>Gets the upper bound, or <see langword="null"/>.</summary>
        public double? Max { get; } = max;

        /// <summary>Gets a value indicating whether the lower bound is invalid.</summary>
        public bool MinExclusive { get; } = minExclusive;

        /// <summary>Gets a value indicating whether the upper bound is invalid.</summary>
        public bool MaxExclusive { get; } = maxExclusive;

        /// <summary>Gets a value indicating whether zero is invalid.</summary>
        public bool NonZero { get; } = nonZero;

        /// <summary>Gets a value indicating whether a value is inside the range.</summary>
        /// <param name="value">The value.</param>
        /// <returns><see langword="true"/> when the value is finite and valid.</returns>
        public bool Contains(double value)
            => !double.IsNaN(value) && !double.IsInfinity(value)
                && !(this.NonZero && value == 0)
                && !(this.Min is { } min && (value < min || (this.MinExclusive && value == min)))
                && !(this.Max is { } max && (value > max || (this.MaxExclusive && value == max)));
    }

    /// <summary>Where an entry and each of its values are in the schema file.</summary>
    /// <param name="entry">The entry's location: its name, or its first key when it has no name.</param>
    /// <param name="values">The location of each key's value.</param>
    internal sealed class Locations(Location entry, IReadOnlyDictionary<string, Location> values)
    {
        /// <summary>Gets the entry's location.</summary>
        public Location Entry { get; } = entry;

        /// <summary>Gets the location of a key's value, or the entry's location when the entry has no such key.</summary>
        /// <param name="key">The key.</param>
        /// <returns>The location.</returns>
        public Location Of(string key) => values.TryGetValue(key, out var location) ? location : this.Entry;
    }
}
