// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;
using YamlDotNet.Core;
using YamlDotNet.RepresentationModel;

namespace Oxygen.Editor.Schemas.Generators;

/// <summary>
/// Reads the scene components schema and reports every problem of the file as a diagnostic at its node.
/// </summary>
/// <remarks>
/// The engine build validates the file against its JSON Schema. The reader checks the shape it reads and what the
/// generated code relies on: names that are unique, field sets and fields that exist, value types and enumerations with
/// an editor type, the number of fields of each value, and encodings the editor implements.
/// </remarks>
internal sealed class SceneComponentsReader
{
    private static readonly string[] RootKeys = ["version", "field_sets", "components"];
    private static readonly string[] FieldSetKeys = ["name", "fields"];
    private static readonly string[] FieldKeys = ["name", "id"];
    private static readonly string[] ComponentKeys = ["name", "kind", "properties"];
    private static readonly string[] ComponentOptionalKeys = ["wire"];
    private static readonly string[] WireKeys = ["id", "field_set"];
    private static readonly string[] PropertyKeys = ["name", "key", "type"];
    private static readonly string[] PropertyOptionalKeys = ["enum", "fields", "encoding"];

    private readonly string path;
    private readonly List<Diagnostic> diagnostics;

    private SceneComponentsReader(string path, List<Diagnostic> diagnostics)
    {
        this.path = path;
        this.diagnostics = diagnostics;
    }

    /// <summary>Reads a schema file.</summary>
    /// <param name="path">The file path, for diagnostic locations.</param>
    /// <param name="text">The file text.</param>
    /// <param name="diagnostics">Receives every problem found.</param>
    /// <returns>The schema, or <see langword="null"/> when it has problems.</returns>
    public static SceneComponentsSchema? Read(string path, string text, List<Diagnostic> diagnostics)
    {
        var reader = new SceneComponentsReader(path, diagnostics);
        var count = diagnostics.Count;
        var schema = reader.ReadStream(text);
        if (schema is not null)
        {
            reader.Check(schema);
        }

        return diagnostics.Count == count ? schema : null;
    }

    private static int FieldCount(string type)
        => type switch
        {
            "bool" or "int" or "float" or "enum" => 1,
            "vec3" => 3,
            _ => 0,
        };

    private static LinePosition Position(Mark mark)
        => new(checked((int)Math.Max(mark.Line - 1, 0)), checked((int)Math.Max(mark.Column - 1, 0)));

    private static string ItemKey(string key, int index) => key + "/" + index.ToString(CultureInfo.InvariantCulture);

    private static YamlNode LastDescendant(YamlNode node)
        => node switch
        {
            YamlSequenceNode { Children.Count: > 0 } sequence => LastDescendant(sequence.Children[sequence.Children.Count - 1]),
            YamlMappingNode { Children.Count: > 0 } mapping => LastDescendant(mapping.Children.Last().Value),
            _ => node,
        };

    private SceneComponentsSchema? ReadStream(string text)
    {
        var stream = new YamlStream();
        try
        {
            stream.Load(new StringReader(text));
        }
        catch (YamlException ex)
        {
            this.Report(SceneComponentsDiagnostics.InvalidYaml, this.At(ex.Start, ex.End), ex.Message);
            return null;
        }

        if (stream.Documents.Count != 1 || stream.Documents[0].RootNode is not YamlMappingNode root)
        {
            this.Report(SceneComponentsDiagnostics.InvalidShape, Location.None, "The schema must be one YAML document holding a mapping");
            return null;
        }

        this.CheckKeys(root, "The schema", RootKeys, []);
        if (this.Integer(root, "version", "The schema") is int version && version != 1)
        {
            this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(root.Children[new YamlScalarNode("version")]), $"Schema version {version} is not 1");
        }

        return new SceneComponentsSchema(
            this.Items(root, "field_sets", "The schema").Select(this.ReadFieldSet).OfType<SceneComponentsSchema.FieldSetEntry>().ToList(),
            this.Items(root, "components", "The schema").Select(this.ReadComponent).OfType<SceneComponentsSchema.ComponentEntry>().ToList());
    }

    private SceneComponentsSchema.FieldSetEntry? ReadFieldSet(YamlNode node)
    {
        if (this.Mapping(node, "A field set") is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, "A field set", FieldSetKeys, []);
        var name = this.Scalar(map, "name", "A field set");
        var fields = this.Items(map, "fields", name ?? "A field set").Select(this.ReadField).OfType<SceneComponentsSchema.FieldEntry>().ToList();
        return name is null ? null : new SceneComponentsSchema.FieldSetEntry(name, fields, this.LocationsOf(map));
    }

    private SceneComponentsSchema.FieldEntry? ReadField(YamlNode node)
    {
        if (this.Mapping(node, "A field") is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, "A field", FieldKeys, []);
        var name = this.Scalar(map, "name", "A field");
        var id = this.Integer(map, "id", name ?? "A field");
        return name is null || id is null ? null : new SceneComponentsSchema.FieldEntry(name, id.Value, this.LocationsOf(map));
    }

    private SceneComponentsSchema.ComponentEntry? ReadComponent(YamlNode node)
    {
        if (this.Mapping(node, "A component") is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, "A component", ComponentKeys, ComponentOptionalKeys);
        var name = this.Scalar(map, "name", "A component");
        var where = name ?? "A component";
        var kind = this.Scalar(map, "kind", where);
        SceneComponentsSchema.WireEntry? wire = null;
        _ = map.Children.TryGetValue(new YamlScalarNode("wire"), out var wireNode);
        if (wireNode is not null && this.Mapping(wireNode, where + ".wire") is YamlMappingNode wireMap)
        {
            this.CheckKeys(wireMap, where + ".wire", WireKeys, []);
            var id = this.Integer(wireMap, "id", where + ".wire");
            var fieldSet = this.Scalar(wireMap, "field_set", where + ".wire");
            wire = id is null || fieldSet is null ? null : new SceneComponentsSchema.WireEntry(id.Value, fieldSet, this.LocationsOf(wireMap));
        }

        var properties = this.Items(map, "properties", where).Select(item => this.ReadProperty(item, where)).OfType<SceneComponentsSchema.PropertyEntry>().ToList();

        // A wire that could not be read is reported; checking the properties against no wire would only add noise.
        return name is null || kind is null || (wireNode is not null && wire is null)
            ? null
            : new SceneComponentsSchema.ComponentEntry(name, kind, wire, properties, this.LocationsOf(map));
    }

    private SceneComponentsSchema.PropertyEntry? ReadProperty(YamlNode node, string component)
    {
        if (this.Mapping(node, component + " property") is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, component + " property", PropertyKeys, PropertyOptionalKeys);
        var name = this.Scalar(map, "name", component + " property");
        var where = component + "." + (name ?? "?");
        var key = this.Scalar(map, "key", where);
        var type = this.Scalar(map, "type", where);
        var enumeration = map.Children.ContainsKey(new YamlScalarNode("enum")) ? this.Scalar(map, "enum", where) : null;
        var encoding = map.Children.ContainsKey(new YamlScalarNode("encoding")) ? this.Scalar(map, "encoding", where) : null;
        var fields = map.Children.ContainsKey(new YamlScalarNode("fields"))
            ? this.Items(map, "fields", where).Select(item => this.ScalarValue(item, where + ".fields")).OfType<string>().ToList()
            : [];
        return name is null || key is null || type is null
            ? null
            : new SceneComponentsSchema.PropertyEntry(name, key, type, enumeration, fields, encoding, this.LocationsOf(map));
    }

    private void Check(SceneComponentsSchema schema)
    {
        this.CheckUnique(schema.FieldSets, static set => set.Name, static set => set.Locations.Of("name"), "field set");
        foreach (var set in schema.FieldSets)
        {
            this.CheckUnique(set.Fields, static field => field.Name, static field => field.Locations.Of("name"), "field of " + set.Name);
            this.CheckUnique(set.Fields, static field => field.Id.ToString(CultureInfo.InvariantCulture), static field => field.Locations.Of("id"), "field id of " + set.Name);
        }

        this.CheckUnique(schema.Components, static component => component.Name, static component => component.Locations.Of("name"), "component");
        this.CheckUnique(schema.Components, static component => component.Kind, static component => component.Locations.Of("kind"), "component kind");
        var wired = schema.Components.Where(static component => component.Wire is not null).ToList();
        this.CheckUnique(wired, static component => component.Wire!.Id.ToString(CultureInfo.InvariantCulture), static component => component.Wire!.Locations.Of("id"), "component id");
        foreach (var component in schema.Components)
        {
            this.CheckUnique(component.Properties, static property => property.Name, static property => property.Locations.Of("name"), "property of " + component.Name);
            var fieldSet = component.Wire is null ? null : schema.FieldSets.FirstOrDefault(set => string.Equals(set.Name, component.Wire.FieldSet, StringComparison.Ordinal));
            if (component.Wire is not null && fieldSet is null)
            {
                this.Report(SceneComponentsDiagnostics.UnknownField, component.Wire.Locations.Of("field_set"), component.Name, $"field set {component.Wire.FieldSet} does not exist");
            }

            foreach (var property in component.Properties)
            {
                this.CheckProperty(component, property, fieldSet);
            }
        }
    }

    private void CheckProperty(SceneComponentsSchema.ComponentEntry component, SceneComponentsSchema.PropertyEntry property, SceneComponentsSchema.FieldSetEntry? fieldSet)
    {
        var where = component.Name + "." + property.Name;
        if (!SceneComponentsTypes.IsKnown(property))
        {
            this.Report(SceneComponentsDiagnostics.UnknownType, property.Locations.Of(property.Enum is null ? "type" : "enum"), where, property.Enum ?? property.Type);
        }

        if (property.Encoding is not null && !string.Equals(property.Encoding, SceneComponentsTypes.DegreesToRadians, StringComparison.Ordinal))
        {
            this.Report(SceneComponentsDiagnostics.UnknownEncoding, property.Locations.Of("encoding"), where, property.Encoding);
        }

        if (component.Wire is null)
        {
            if (property.Fields.Count != 0)
            {
                this.Report(SceneComponentsDiagnostics.FieldMismatch, property.Locations.Of("fields"), where, "a component without a wire has no fields");
            }

            return;
        }

        if (property.Fields.Count != FieldCount(property.Type))
        {
            this.Report(SceneComponentsDiagnostics.FieldMismatch, property.Locations.Of("fields"), where, $"{property.Type} values go to {FieldCount(property.Type)} fields, not {property.Fields.Count}");
        }

        for (var index = 0; fieldSet is not null && index < property.Fields.Count; index++)
        {
            var field = property.Fields[index];
            if (!fieldSet.Fields.Any(entry => string.Equals(entry.Name, field, StringComparison.Ordinal)))
            {
                this.Report(SceneComponentsDiagnostics.UnknownField, property.Locations.Of(ItemKey("fields", index)), where, $"field {field} is not in field set {fieldSet.Name}");
            }
        }
    }

    private void CheckUnique<T>(IEnumerable<T> items, Func<T, string> key, Func<T, Location> location, string what)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var item in items.Where(item => !seen.Add(key(item))))
        {
            this.Report(SceneComponentsDiagnostics.InvalidShape, location(item), $"Duplicate {what}: {key(item)}");
        }
    }

    private void CheckKeys(YamlMappingNode map, string where, string[] required, string[] optional)
    {
        foreach (var missing in required.Where(key => !map.Children.ContainsKey(new YamlScalarNode(key))))
        {
            this.Report(SceneComponentsDiagnostics.InvalidShape, this.LocationsOf(map).Entry, $"{where} has no '{missing}'");
        }

        foreach (var entry in map.Children.Keys)
        {
            var name = (entry as YamlScalarNode)?.Value;
            if (name is null || (!required.Contains(name, StringComparer.Ordinal) && !optional.Contains(name, StringComparer.Ordinal)))
            {
                this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(entry), $"{where} has an unknown key '{name}'");
            }
        }
    }

    private YamlMappingNode? Mapping(YamlNode node, string what)
    {
        if (node is YamlMappingNode map)
        {
            return map;
        }

        this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(node), $"{what} must be a mapping");
        return null;
    }

    private IEnumerable<YamlNode> Items(YamlMappingNode map, string key, string where)
    {
        if (!map.Children.TryGetValue(new YamlScalarNode(key), out var node))
        {
            return [];
        }

        if (node is YamlSequenceNode sequence)
        {
            return sequence.Children;
        }

        this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(node), $"{where}.{key} must be a sequence");
        return [];
    }

    private string? Scalar(YamlMappingNode map, string key, string where)
        => map.Children.TryGetValue(new YamlScalarNode(key), out var node) ? this.ScalarValue(node, where + "." + key) : null;

    private string? ScalarValue(YamlNode node, string where)
    {
        if (node is YamlScalarNode { Value: { Length: > 0 } value })
        {
            return value;
        }

        this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(node), $"{where} must be a non-empty scalar");
        return null;
    }

    private int? Integer(YamlMappingNode map, string key, string where)
    {
        if (this.Scalar(map, key, where) is not string text)
        {
            return null;
        }

        if (int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out var value))
        {
            return value;
        }

        this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(map.Children[new YamlScalarNode(key)]), $"{where}.{key} must be a non-negative integer, not '{text}'");
        return null;
    }

    private void Report(DiagnosticDescriptor descriptor, Location location, params object[] arguments)
        => this.diagnostics.Add(Diagnostic.Create(descriptor, location, arguments));

    // Each key's value, and each item of a sequence value under ItemKey(key, index).
    private SceneComponentsSchema.Locations LocationsOf(YamlMappingNode map)
    {
        var values = new Dictionary<string, Location>(StringComparer.Ordinal);
        foreach (var child in map.Children)
        {
            if (child.Key is not YamlScalarNode { Value: { } key } || values.ContainsKey(key))
            {
                continue;
            }

            values.Add(key, this.At(child.Value));
            if (child.Value is YamlSequenceNode sequence)
            {
                for (var index = 0; index < sequence.Children.Count; index++)
                {
                    values.Add(ItemKey(key, index), this.At(sequence.Children[index]));
                }
            }
        }

        var entry = values.TryGetValue("name", out var name)
            ? name
            : map.Children.Count != 0 ? this.At(map.Children.First().Key) : this.At(map);
        return new SceneComponentsSchema.Locations(entry, values);
    }

    // A flow collection's end mark is not set; such a node spans to the end of its last item.
    private Location At(YamlNode node) => this.At(node.Start, node.End.Index > node.Start.Index ? node.End : LastDescendant(node).End);

    private Location At(Mark start, Mark end)
        => Location.Create(
            this.path,
            TextSpan.FromBounds(checked((int)start.Index), checked((int)Math.Max(start.Index, end.Index))),
            new LinePositionSpan(Position(start), Position(Math.Max(start.Index, end.Index) == start.Index ? start : end)));
}
