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
internal sealed class SceneComponentsSchema(IReadOnlyList<SceneComponentsSchema.FieldSetEntry> fieldSets, IReadOnlyList<SceneComponentsSchema.ComponentEntry> components)
{
    /// <summary>Gets the field sets, in schema order.</summary>
    public IReadOnlyList<FieldSetEntry> FieldSets { get; } = fieldSets;

    /// <summary>Gets the components, in schema order.</summary>
    public IReadOnlyList<ComponentEntry> Components { get; } = components;

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
    /// <param name="wire">How the engine takes the component field by field, or <see langword="null"/>.</param>
    /// <param name="properties">The properties, in schema order.</param>
    /// <param name="parts">The parts, in schema order.</param>
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class ComponentEntry(string name, string kind, WireEntry? wire, IReadOnlyList<PropertyEntry> properties, IReadOnlyList<PartEntry> parts, SceneComponentsSchema.Locations locations)
    {
        /// <summary>Gets the component name.</summary>
        public string Name { get; } = name;

        /// <summary>Gets the component kind.</summary>
        public string Kind { get; } = kind;

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
    /// <param name="locations">The entry's locations in the schema file.</param>
    internal sealed class PropertyEntry(string name, string key, string type, string? enumeration, IReadOnlyList<string> fields, string? encoding, IReadOnlyList<string> sources, SceneComponentsSchema.Locations locations)
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

        /// <summary>Gets the entry's locations in the schema file.</summary>
        public Locations Locations { get; } = locations;
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
