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
/// an editor type, the number of fields of each value, encodings the editor implements, parts whose properties each
/// have a source and no fields, override ids, and defaults, ranges and units that fit their property.
/// </remarks>
internal sealed partial class SceneComponentsReader
{
    private const int SchemaVersion = 2;

    private static readonly string[] RootKeys = ["version", "units", "enums", "field_sets", "components"];
    private static readonly string[] UnitKeys = ["id", "abbreviation", "name"];
    private static readonly string[] EnumKeys = ["name", "members"];
    private static readonly string[] MemberKeys = ["name", "value"];
    private static readonly string[] FieldSetKeys = ["name", "fields"];
    private static readonly string[] FieldKeys = ["name", "id"];
    private static readonly string[] ComponentKeys = ["name", "kind", "cardinality", "properties"];
    private static readonly string[] ComponentOptionalKeys = ["wire", "parts"];
    private static readonly string[] WireKeys = ["id", "field_set"];
    private static readonly string[] PartKeys = ["name", "kind", "properties"];
    private static readonly string[] PropertyKeys = ["name", "key", "type"];
    private static readonly string[] ValueKeys = ["default", "root_default", "range", "commit", "unit", "display_unit"];
    private static readonly string[] PropertyOptionalKeys = ["asset_type", "enum", "fields", "encoding", "sources", "override_id", .. ValueKeys];
    private static readonly string[] PartPropertyKeys = ["name", "key", "type", "sources", "override_id"];
    private static readonly string[] PartPropertyOptionalKeys = ["asset_type", "enum", .. ValueKeys];
    private static readonly string[] RangeKeys = ["min", "max", "min_exclusive", "max_exclusive", "nonzero"];
    private static readonly string[] Sources = ["asset", "model"];
    private static readonly string[] AssetTypes = ["geometry", "material"];
    private static readonly string[] Cardinalities = ["required", "optional"];
    private static readonly string[] Commits = ["clamp", "reject"];

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

    private static bool Has(YamlMappingNode map, string key) => map.Children.ContainsKey(new YamlScalarNode(key));

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
        if (this.Integer(root, "version", "The schema") is int version && version != SchemaVersion)
        {
            this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(root.Children[new YamlScalarNode("version")]), $"Schema version {version.ToString(CultureInfo.InvariantCulture)} is not {SchemaVersion.ToString(CultureInfo.InvariantCulture)}");
        }

        return new SceneComponentsSchema(
            [.. this.Items(root, "units", "The schema").Select(this.ReadUnit).OfType<SceneComponentsSchema.UnitEntry>()],
            [.. this.Items(root, "enums", "The schema").Select(this.ReadEnum).OfType<SceneComponentsSchema.EnumEntry>()],
            [.. this.Items(root, "field_sets", "The schema").Select(this.ReadFieldSet).OfType<SceneComponentsSchema.FieldSetEntry>()],
            [.. this.Items(root, "components", "The schema").Select(this.ReadComponent).OfType<SceneComponentsSchema.ComponentEntry>()]);
    }

    private SceneComponentsSchema.UnitEntry? ReadUnit(YamlNode node)
    {
        if (this.Mapping(node, "A unit") is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, "A unit", UnitKeys, []);
        var id = this.Scalar(map, "id", "A unit");
        var abbreviation = this.Scalar(map, "abbreviation", id ?? "A unit");
        var name = this.Scalar(map, "name", id ?? "A unit");
        return id is null || abbreviation is null || name is null ? null : new SceneComponentsSchema.UnitEntry(id, abbreviation, name, this.LocationsOf(map));
    }

    private SceneComponentsSchema.EnumEntry? ReadEnum(YamlNode node)
    {
        if (this.Mapping(node, "An enumeration") is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, "An enumeration", EnumKeys, []);
        var name = this.Scalar(map, "name", "An enumeration");
        var members = this.Items(map, "members", name ?? "An enumeration").Select(this.ReadMember).OfType<SceneComponentsSchema.MemberEntry>().ToList();
        return name is null ? null : new SceneComponentsSchema.EnumEntry(name, members, this.LocationsOf(map));
    }

    private SceneComponentsSchema.MemberEntry? ReadMember(YamlNode node)
    {
        if (this.Mapping(node, "A member") is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, "A member", MemberKeys, []);
        var name = this.Scalar(map, "name", "A member");
        var value = this.Integer(map, "value", name ?? "A member");
        return name is null || value is null ? null : new SceneComponentsSchema.MemberEntry(name, value.Value, this.LocationsOf(map));
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
        var cardinality = this.Choice(map, "cardinality", where, Cardinalities);
        SceneComponentsSchema.WireEntry? wire = null;
        _ = map.Children.TryGetValue(new YamlScalarNode("wire"), out var wireNode);
        if (wireNode is not null && this.Mapping(wireNode, where + ".wire") is YamlMappingNode wireMap)
        {
            this.CheckKeys(wireMap, where + ".wire", WireKeys, []);
            var id = this.Integer(wireMap, "id", where + ".wire");
            var fieldSet = this.Scalar(wireMap, "field_set", where + ".wire");
            wire = id is null || fieldSet is null ? null : new SceneComponentsSchema.WireEntry(id.Value, fieldSet, this.LocationsOf(wireMap));
        }

        var properties = this.Items(map, "properties", where)
            .Select(item => this.ReadProperty(item, where, PropertyKeys, PropertyOptionalKeys))
            .OfType<SceneComponentsSchema.PropertyEntry>()
            .ToList();
        var parts = this.Items(map, "parts", where).Select(item => this.ReadPart(item, where)).OfType<SceneComponentsSchema.PartEntry>().ToList();

        // A wire that could not be read is reported; checking the properties against no wire would only add noise.
        return name is null || kind is null || cardinality is null || (wireNode is not null && wire is null)
            ? null
            : new SceneComponentsSchema.ComponentEntry(name, kind, string.Equals(cardinality, "required", StringComparison.Ordinal), wire, properties, parts, this.LocationsOf(map));
    }

    // A part's properties always have a source and never take fields, so a key that breaks that is an unknown key.
    private SceneComponentsSchema.PartEntry? ReadPart(YamlNode node, string component)
    {
        if (this.Mapping(node, component + " part") is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, component + " part", PartKeys, []);
        var name = this.Scalar(map, "name", component + " part");
        var where = component + "." + (name ?? "?");
        var kind = this.Scalar(map, "kind", where);
        var properties = this.Items(map, "properties", where)
            .Select(item => this.ReadProperty(item, where, PartPropertyKeys, PartPropertyOptionalKeys))
            .OfType<SceneComponentsSchema.PropertyEntry>()
            .ToList();
        return name is null || kind is null ? null : new SceneComponentsSchema.PartEntry(name, kind, properties, this.LocationsOf(map));
    }

    private SceneComponentsSchema.PropertyEntry? ReadProperty(YamlNode node, string component, string[] required, string[] optional)
    {
        if (this.Mapping(node, component + " property") is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, component + " property", required, optional);
        var name = this.Scalar(map, "name", component + " property");
        var where = component + "." + (name ?? "?");
        var key = this.Scalar(map, "key", where);
        var type = this.Scalar(map, "type", where);
        var assetType = Has(map, "asset_type") ? this.Scalar(map, "asset_type", where) : null;
        var enumeration = Has(map, "enum") ? this.Scalar(map, "enum", where) : null;
        var encoding = Has(map, "encoding") ? this.Scalar(map, "encoding", where) : null;
        var fields = Has(map, "fields")
            ? this.Items(map, "fields", where).Select(item => this.ScalarValue(item, where + ".fields")).OfType<string>().ToList()
            : [];
        var sources = this.ReadSources(map, where);
        var values = this.ReadValues(map, where, type);
        return name is null || key is null || type is null || values is null
            ? null
            : new SceneComponentsSchema.PropertyEntry(name, key, type, assetType, enumeration, fields, encoding, sources, values, this.LocationsOf(map));
    }

    private List<string> ReadSources(YamlMappingNode map, string where)
    {
        if (!map.Children.TryGetValue(new YamlScalarNode("sources"), out var node))
        {
            return [];
        }

        var items = this.Items(map, "sources", where).ToList();
        if (node is YamlSequenceNode && items.Count == 0)
        {
            this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(node), $"{where}.sources must name a source");
        }

        var sources = new List<string>();
        foreach (var item in items)
        {
            if (this.ScalarValue(item, where + ".sources") is not string source)
            {
                continue;
            }

            if (!Sources.Contains(source, StringComparer.Ordinal))
            {
                this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(item), $"{where}.sources has an unknown source '{source}'");
            }
            else if (sources.Contains(source, StringComparer.Ordinal))
            {
                this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(item), $"{where}.sources names '{source}' twice");
            }

            sources.Add(source);
        }

        return sources;
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
        foreach (var missing in required.Where(key => !Has(map, key)))
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

    private string? Choice(YamlMappingNode map, string key, string where, string[] choices)
    {
        if (this.Scalar(map, key, where) is not string value)
        {
            return null;
        }

        if (choices.Contains(value, StringComparer.Ordinal))
        {
            return value;
        }

        this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(map.Children[new YamlScalarNode(key)]), $"{where}.{key} must be one of {string.Join(", ", choices)}, not '{value}'");
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
