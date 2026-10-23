// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;

namespace Oxygen.Editor.Schemas.Generators;

/// <content>Renders each property's default, range and units, and the lookups of ranges and units by property id.</content>
internal static partial class SceneComponentsEmitter
{
    private const string PropertyIdType = "global::Oxygen.Managed.Core.PropertyId";
    private const string RangeType = "global::Oxygen.Managed.Core.PropertyRange";
    private const string UnitType = "global::Oxygen.Managed.Core.PropertyUnit";
    private const string UnitTable = "global::Oxygen.Editor.World.SceneUnits";
    private const string LodPolicyType = "global::Oxygen.Editor.World.Serialization.LodPolicy";

    // The range, unit and shown unit of every property, by property id, so code that holds only an id finds them.
    private static StringBuilder AppendLookups(StringBuilder source, SceneComponentsSchema schema)
    {
        var properties = Paths(schema).ToList();
        _ = source.AppendLine()
            .AppendLine("    /// <summary>Gets the range of a numeric property; a vector property's range bounds each component.</summary>")
            .AppendLine("    /// <typeparam name=\"T\">The bounded type: <see cref=\"int\"/> or <see cref=\"float\"/>.</typeparam>")
            .AppendLine("    /// <param name=\"id\">The property id.</param>")
            .AppendLine("    /// <returns>The range, or <see langword=\"null\"/> when the property has no range of that type.</returns>")
            .AppendLine("    public static global::Oxygen.Managed.Core.PropertyRange<T>? RangeOf<T>(" + PropertyIdType + " id)")
            .AppendLine("        where T : struct, global::System.Numerics.INumber<T>")
            .AppendLine("        => RangesById.GetValueOrDefault(id) as global::Oxygen.Managed.Core.PropertyRange<T>;")
            .AppendLine()
            .AppendLine("    /// <summary>Gets the unit of a property's stored value.</summary>")
            .AppendLine("    /// <param name=\"id\">The property id.</param>")
            .AppendLine("    /// <returns>The unit, or <see langword=\"null\"/> when the value has none.</returns>")
            .AppendLine("    public static " + UnitType + "? UnitOf(" + PropertyIdType + " id) => UnitsById.GetValueOrDefault(id);")
            .AppendLine()
            .AppendLine("    /// <summary>Gets the unit UIs show a property's value in.</summary>")
            .AppendLine("    /// <param name=\"id\">The property id.</param>")
            .AppendLine("    /// <returns>The unit, or <see langword=\"null\"/> when the value has none.</returns>")
            .AppendLine("    public static " + UnitType + "? DisplayUnitOf(" + PropertyIdType + " id) => DisplayUnitsById.GetValueOrDefault(id);");
        _ = AppendLookup(source, "RangesById", "object", properties.Where(static entry => SceneComponentsTypes.IsNumeric(entry.Property)), static path => path.Owner + ".Ranges." + path.Property.Name);
        _ = AppendLookup(source, "UnitsById", UnitType, properties.Where(static entry => entry.Property.Values.Unit is not null), static path => path.Owner + ".Units." + path.Property.Name);
        return AppendLookup(source, "DisplayUnitsById", UnitType, properties.Where(static entry => entry.Property.Values.Unit is not null), static path => path.Owner + ".DisplayUnits." + path.Property.Name);
    }

    private static StringBuilder AppendLookup(
        StringBuilder source,
        string name,
        string valueType,
        IEnumerable<(string Owner, SceneComponentsSchema.PropertyEntry Property)> properties,
        Func<(string Owner, SceneComponentsSchema.PropertyEntry Property), string> value)
    {
        _ = source.AppendLine()
            .Append("    private static global::System.Collections.Frozen.FrozenDictionary<").Append(PropertyIdType).Append(", ").Append(valueType).Append("> ").Append(name).AppendLine(" { get; } =")
            .Append("        global::System.Collections.Frozen.FrozenDictionary.ToFrozenDictionary(new global::System.Collections.Generic.Dictionary<").Append(PropertyIdType).Append(", ").Append(valueType).AppendLine(">")
            .AppendLine("        {");
        foreach (var entry in properties)
        {
            _ = source.Append("            [").Append(entry.Owner).Append('.').Append(entry.Property.Name).Append(".Id] = ").Append(value(entry)).AppendLine(",");
        }

        return source.AppendLine("        });");
    }

    // Each property with the path of the class that holds its id: the component's, or its part's.
    private static IEnumerable<(string Owner, SceneComponentsSchema.PropertyEntry Property)> Paths(SceneComponentsSchema schema)
    {
        foreach (var component in schema.Components)
        {
            foreach (var property in component.Properties)
            {
                yield return (component.Name, property);
            }

            foreach (var part in component.Parts)
            {
                foreach (var property in part.Properties)
                {
                    yield return (component.Name + "." + part.Name, property);
                }
            }
        }
    }

    private static StringBuilder AppendValues(StringBuilder source, string indent, IReadOnlyList<SceneComponentsSchema.PropertyEntry> properties)
    {
        var inner = indent + "    ";
        _ = AppendClass(source, indent, "Defaults", "The creation value of each property; an asset or string value is supplied by the command that creates it.", properties.Where(static property => property.Values.Default is not null), property => Default(inner, property));
        _ = AppendClass(source, indent, "RootDefaults", "The value of each node flag on a new root, and what an Inherit root resolves to.", properties.Where(static property => property.Values.RootDefault is not null), property => RootDefault(inner, property));
        _ = AppendClass(source, indent, "Ranges", "The valid values of each numeric property; a vector property's range bounds each component.", properties.Where(SceneComponentsTypes.IsNumeric), property => Range(inner, property));
        _ = AppendClass(source, indent, "Units", "The unit of each property's stored value, the one its default and range use.", properties.Where(static property => property.Values.Unit is not null), property => Unit(inner, property, property.Values.Unit!));
        return AppendClass(source, indent, "DisplayUnits", "The unit UIs show each property's value in.", properties.Where(static property => property.Values.Unit is not null), property => Unit(inner, property, property.Values.DisplayUnit ?? property.Values.Unit!));
    }

    private static StringBuilder AppendClass(StringBuilder source, string indent, string name, string summary, IEnumerable<SceneComponentsSchema.PropertyEntry> properties, Func<SceneComponentsSchema.PropertyEntry, string> member)
    {
        var members = properties.Select(member).ToList();
        if (members.Count == 0)
        {
            return source;
        }

        _ = source.AppendLine()
            .Append(indent).Append("/// <summary>").Append(summary).AppendLine("</summary>")
            .Append(indent).Append("public static class ").AppendLine(name)
            .Append(indent).AppendLine("{");
        for (var index = 0; index < members.Count; index++)
        {
            _ = (index == 0 ? source : source.AppendLine()).Append(members[index]);
        }

        return source.Append(indent).AppendLine("}");
    }

    private static string Default(string indent, SceneComponentsSchema.PropertyEntry property)
    {
        var value = property.Values.Default!;
        var declaration = property.Type switch
        {
            "bool" => "public const bool " + property.Name + " = " + (bool.Parse(value.Scalar!) ? "true" : "false") + ";",
            "int" => "public const int " + property.Name + " = " + value.Scalar + ";",
            "float" => "public const float " + property.Name + " = " + Float(double.Parse(value.Scalar!, NumberStyles.Float, CultureInfo.InvariantCulture)) + ";",
            "vec3" => "public static global::System.Numerics.Vector3 " + property.Name + " { get; } = new(" + string.Join(", ", value.Numbers!.Select(Float)) + ");",
            "enum" => "public const " + SceneComponentsTypes.ValueType(property) + " " + property.Name + " = " + SceneComponentsTypes.ValueType(property) + "." + value.Scalar + ";",
            _ => "public static " + LodPolicyType + " " + property.Name + " { get; } = " + Policy(value.Policy!) + ";",
        };
        return Member(indent, "The default of <c>" + property.Key + "</c>.", declaration);
    }

    private static string RootDefault(string indent, SceneComponentsSchema.PropertyEntry property)
    {
        var type = SceneComponentsTypes.ValueType(property);
        return Member(indent, "The root default of <c>" + property.Key + "</c>.", "public const " + type + " " + property.Name + " = " + type + "." + property.Values.RootDefault + ";");
    }

    private static string Range(string indent, SceneComponentsSchema.PropertyEntry property)
    {
        var range = property.Values.Range;
        var isInt = string.Equals(property.Type, "int", StringComparison.Ordinal);
        string Bound(double? bound) => bound is not { } value ? "null" : isInt ? Number(checked((int)value)) : Float(value);
        var commit = property.Values.Clamps ? "Clamp" : "Reject";
        var declaration = "public static " + RangeType + "<" + SceneComponentsTypes.RangeType(property) + "> " + property.Name + " { get; } = new("
            + Bound(range?.Min) + ", " + Bound(range?.Max) + ", "
            + Flag(range?.MinExclusive) + ", " + Flag(range?.MaxExclusive) + ", " + Flag(range?.NonZero) + ", "
            + "global::Oxygen.Managed.Core.RangeCommit." + commit + ");";
        return Member(indent, "The range of <c>" + property.Key + "</c>.", declaration);
    }

    private static string Unit(string indent, SceneComponentsSchema.PropertyEntry property, string unit)
        => Member(indent, "The unit of <c>" + property.Key + "</c>.", "public static " + UnitType + " " + property.Name + " => " + UnitTable + "." + SceneComponentsTypes.Pascal(unit) + ";");

    private static string Member(string indent, string summary, string declaration)
        => new StringBuilder()
            .Append(indent).Append("/// <summary>").Append(summary).AppendLine("</summary>")
            .Append(indent).AppendLine(declaration)
            .ToString();

    private static string Policy(SceneComponentsSchema.LodPolicyEntry policy)
        => policy.Type switch
        {
            "fixed" => "new " + LodPolicyType + ".Fixed(" + Number(policy.LodIndex) + ")",
            "distance" => "new " + LodPolicyType + ".Distance([" + Floats(policy.Thresholds) + "], " + Float(policy.HysteresisRatio) + ")",
            _ => "new " + LodPolicyType + ".ScreenSpaceError([" + Floats(policy.EnterFinerSse) + "], [" + Floats(policy.ExitCoarserSse) + "])",
        };

    private static StringBuilder AppendUnitTable(StringBuilder source, SceneComponentsSchema schema)
    {
        _ = source.AppendLine()
            .AppendLine("/// <summary>The units of the scene component properties; UIs take every unit string from here.</summary>")
            .AppendLine("public static class SceneUnits")
            .AppendLine("{");
        for (var index = 0; index < schema.Units.Count; index++)
        {
            var unit = schema.Units[index];
            _ = (index == 0 ? source : source.AppendLine())
                .Append("    /// <summary>Gets the <c>").Append(unit.Id).AppendLine("</c> unit.</summary>")
                .Append("    public static ").Append(UnitType).Append(' ').Append(SceneComponentsTypes.Pascal(unit.Id))
                .Append(" { get; } = new(").Append(Literal(unit.Id)).Append(", ").Append(Literal(unit.Abbreviation)).Append(", ").Append(Literal(unit.Name)).AppendLine(");");
        }

        return source.AppendLine("}");
    }

    private static string Float(double value) => ((float)value).ToString("R", CultureInfo.InvariantCulture) + "f";

    private static string Floats(IEnumerable<double> values) => string.Join(", ", values.Select(Float));

    private static string Flag(bool? value) => value == true ? "true" : "false";

    private static string Literal(string text) => "\"" + text.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";
}
