// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace Oxygen.Editor.Schemas.Generators;

/// <content>Checks what the generated code relies on once the schema is read.</content>
internal sealed partial class SceneComponentsReader
{
    // The most boundaries of a LOD policy: a geometry has at most 8 LODs.
    private const int MaxLodBoundaries = 7;

    private static readonly string[] PolicyTypes = ["fixed", "distance", "screen_space_error"];

    private static double? ParseNumber(string? text)
        => double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var value) ? value : null;

    private void Check(SceneComponentsSchema schema)
    {
        this.CheckUnique(schema.Units, static unit => unit.Id, static unit => unit.Locations.Of("id"), "unit");
        this.CheckUnique(schema.Enums, static entry => entry.Name, static entry => entry.Locations.Of("name"), "enumeration");
        foreach (var entry in schema.Enums)
        {
            this.CheckUnique(entry.Members, static member => member.Name, static member => member.Locations.Of("name"), "member of " + entry.Name);
            this.CheckUnique(entry.Members, static member => member.Value.ToString(CultureInfo.InvariantCulture), static member => member.Locations.Of("value"), "member value of " + entry.Name);
        }

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
            this.CheckComponent(schema, component);
        }

        var overridable = schema.Components
            .SelectMany(static component => component.Properties.Concat(component.Parts.SelectMany(static part => part.Properties)))
            .Where(static property => property.Values.OverrideId is not null);
        this.CheckUnique(overridable, static property => property.Values.OverrideId!.Value.ToString(CultureInfo.InvariantCulture), static property => property.Locations.Of("override_id"), "override id");
    }

    private void CheckComponent(SceneComponentsSchema schema, SceneComponentsSchema.ComponentEntry component)
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
            this.CheckValues(schema, component.Name + "." + property.Name, property);
        }

        this.CheckParts(schema, component);
    }

    // A part's name scopes its properties' names in the generated code, so it cannot be a property's name.
    private void CheckParts(SceneComponentsSchema schema, SceneComponentsSchema.ComponentEntry component)
    {
        var names = component.Properties.Select(static property => (property.Name, Location: property.Locations.Of("name")))
            .Concat(component.Parts.Select(static part => (part.Name, Location: part.Locations.Of("name"))));
        this.CheckUnique(names, static entry => entry.Name, static entry => entry.Location, "part or property of " + component.Name);
        this.CheckUnique(component.Parts, static part => part.Kind, static part => part.Locations.Of("kind"), "part kind of " + component.Name);
        foreach (var part in component.Parts)
        {
            var where = component.Name + "." + part.Name;
            this.CheckUnique(part.Properties, static property => property.Name, static property => property.Locations.Of("name"), "property of " + where);
            this.CheckUnique(part.Properties, static property => property.Key, static property => property.Locations.Of("key"), "key of " + where);
            foreach (var property in part.Properties)
            {
                if (!SceneComponentsTypes.IsKnown(property))
                {
                    this.Report(SceneComponentsDiagnostics.UnknownType, property.Locations.Of(property.Enum is null ? "type" : "enum"), where + "." + property.Name, property.Enum ?? property.Type);
                }

                this.CheckValues(schema, where + "." + property.Name, property);
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

        if (property.Encoding is not null && !SceneComponentsTypes.IsEncoding(property.Encoding))
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
            this.Report(SceneComponentsDiagnostics.FieldMismatch, property.Locations.Of("fields"), where, $"{property.Type} values go to {FieldCount(property.Type).ToString(CultureInfo.InvariantCulture)} fields, not {property.Fields.Count.ToString(CultureInfo.InvariantCulture)}");
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

    private void CheckValues(SceneComponentsSchema schema, string where, SceneComponentsSchema.PropertyEntry property)
    {
        var values = property.Values;
        var changeable = property.Sources.Count != 0 || property.Fields.Count != 0;
        var overridden = values.OverrideId is not null;
        if (changeable != overridden)
        {
            this.Invalid(property, values.OverrideId is null ? "fields" : "override_id", where, "a property has an override id exactly when it has sources or fields");
        }

        // The engine generator requires the asset type; the editor reads none.
        if (property.AssetType is not null && !string.Equals(property.Type, "asset", StringComparison.Ordinal))
        {
            this.Invalid(property, "asset_type", where, $"{property.Type} values have no asset type");
        }
        else if (property.AssetType is not null && !AssetTypes.Contains(property.AssetType, StringComparer.Ordinal))
        {
            this.Invalid(property, "asset_type", where, $"unknown asset type '{property.AssetType}'");
        }

        var valued = property.Type is not ("asset" or "string");
        if (valued != (values.Default is not null))
        {
            this.Invalid(property, valued ? "name" : "default", where, valued ? "a default is required" : $"{property.Type} values have no default");
        }

        if (!SceneComponentsTypes.IsNumeric(property) && (values.Range is not null || values.Unit is not null))
        {
            this.Invalid(property, values.Range is not null ? "range" : "unit", where, $"{property.Type} values have no range or unit");
        }

        if (values.Clamps && values.Range is null)
        {
            this.Invalid(property, "commit", where, "a commit policy needs a range");
        }

        this.CheckUnits(schema, where, property);
        if (string.Equals(property.Type, "enum", StringComparison.Ordinal))
        {
            this.CheckEnumValues(schema, where, property);
        }
        else if (values.RootDefault is not null)
        {
            this.Invalid(property, "root_default", where, "only an enum property has a root default");
        }

        if (values.Default is { } value)
        {
            this.CheckDefault(where, property, value);
        }
    }

    private void CheckUnits(SceneComponentsSchema schema, string where, SceneComponentsSchema.PropertyEntry property)
    {
        var values = property.Values;
        foreach (var (key, unit) in new[] { ("unit", values.Unit), ("display_unit", values.DisplayUnit) })
        {
            if (unit is not null && !schema.Units.Any(entry => string.Equals(entry.Id, unit, StringComparison.Ordinal)))
            {
                this.Invalid(property, key, where, $"unit '{unit}' is not declared");
            }
        }

        if (values.DisplayUnit is not null && !(string.Equals(values.Unit, "radian", StringComparison.Ordinal) && string.Equals(values.DisplayUnit, "degree", StringComparison.Ordinal)))
        {
            this.Invalid(property, "display_unit", where, $"{values.Unit ?? "unitless"} values cannot be shown in {values.DisplayUnit}");
        }
    }

    private void CheckEnumValues(SceneComponentsSchema schema, string where, SceneComponentsSchema.PropertyEntry property)
    {
        var enumeration = schema.Enums.FirstOrDefault(entry => string.Equals(entry.Name, property.Enum, StringComparison.Ordinal));
        if (enumeration is null)
        {
            this.Invalid(property, "enum", where, $"enumeration '{property.Enum}' is not declared");
            return;
        }

        foreach (var (key, member) in new[] { ("default", property.Values.Default?.Scalar), ("root_default", property.Values.RootDefault) })
        {
            if (member is not null && !enumeration.Members.Any(entry => string.Equals(entry.Name, member, StringComparison.Ordinal)))
            {
                this.Invalid(property, key, where, $"'{member}' is not a member of {enumeration.Name}");
            }
        }
    }

    private void CheckDefault(string where, SceneComponentsSchema.PropertyEntry property, SceneComponentsSchema.ValueEntry value)
    {
        switch (property.Type)
        {
            case "bool" when !bool.TryParse(value.Scalar, out _):
                this.Invalid(property, "default", where, $"'{value.Scalar}' is not true or false");
                return;
            case "int" or "float":
                this.CheckNumbers(where, property, [ParseNumber(value.Scalar)]);
                return;
            case "vec3":
                this.CheckNumbers(where, property, [.. value.Numbers!.Cast<double?>()]);
                return;
            case "lod-policy":
                this.CheckPolicy(where, property, value.Policy!);
                return;
            default:
                return;
        }
    }

    private void CheckNumbers(string where, SceneComponentsSchema.PropertyEntry property, List<double?> numbers)
    {
        var range = property.Values.Range;
        var isInt = string.Equals(property.Type, "int", StringComparison.Ordinal);
        if (string.Equals(property.Type, "vec3", StringComparison.Ordinal) && numbers.Count != 3)
        {
            this.Invalid(property, "default", where, $"a vec3 default has 3 numbers, not {numbers.Count.ToString(CultureInfo.InvariantCulture)}");
        }

        if (range is { Min: { } min, Max: { } max } && (min > max || (min == max && (range.MinExclusive || range.MaxExclusive))))
        {
            this.Invalid(property, "range", where, "the range is empty");
        }

        if (isInt && range is not null && new[] { range.Min, range.Max }.Any(static bound => bound is { } value && Math.Floor(value) != value))
        {
            this.Invalid(property, "range", where, "an int range has whole bounds");
        }

        foreach (var number in numbers)
        {
            if (number is not { } value || (isInt && Math.Floor(value) != value))
            {
                this.Invalid(property, "default", where, $"the default is not {(isInt ? "an integer" : "a number")}");
            }
            else if (!(range?.Contains(value) ?? (!double.IsNaN(value) && !double.IsInfinity(value))))
            {
                this.Invalid(property, "default", where, $"the default {value.ToString("R", CultureInfo.InvariantCulture)} is outside its range");
            }
        }
    }

    private void CheckPolicy(string where, SceneComponentsSchema.PropertyEntry property, SceneComponentsSchema.LodPolicyEntry policy)
    {
        if (!PolicyTypes.Contains(policy.Type, StringComparer.Ordinal))
        {
            this.Invalid(property, "default", where, $"'{policy.Type}' is not a LOD policy type");
        }

        var boundaries = new[] { policy.Thresholds, policy.EnterFinerSse, policy.ExitCoarserSse };
        if (policy.LodIndex > MaxLodBoundaries || boundaries.Any(static list => list.Count > MaxLodBoundaries))
        {
            this.Invalid(property, "default", where, $"a LOD policy addresses at most {(MaxLodBoundaries + 1).ToString(CultureInfo.InvariantCulture)} LODs");
        }

        if (policy.EnterFinerSse.Count != policy.ExitCoarserSse.Count)
        {
            this.Invalid(property, "default", where, "enter_finer_sse and exit_coarser_sse differ in size");
        }

        if (policy.HysteresisRatio is < 0 or > 0.99)
        {
            this.Invalid(property, "default", where, "the hysteresis ratio is outside [0, 0.99]");
        }
    }

    private void Invalid(SceneComponentsSchema.PropertyEntry property, string key, string where, string problem)
        => this.Report(SceneComponentsDiagnostics.InvalidValue, property.Locations.Of(key), where, problem);
}
