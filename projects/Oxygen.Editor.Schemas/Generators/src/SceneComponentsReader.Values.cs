// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using YamlDotNet.RepresentationModel;

namespace Oxygen.Editor.Schemas.Generators;

/// <content>Reads a property's override id, default, range, commit policy and units.</content>
internal sealed partial class SceneComponentsReader
{
    private static readonly string[] PolicyKeys = ["type", "lod_index", "thresholds", "hysteresis_ratio", "enter_finer_sse", "exit_coarser_sse"];

    private SceneComponentsSchema.ValuesEntry? ReadValues(YamlMappingNode map, string where, string? type)
    {
        var count = this.diagnostics.Count;
        var overrideId = Has(map, "override_id") ? this.Integer(map, "override_id", where) : null;
        var value = map.Children.TryGetValue(new YamlScalarNode("default"), out var node) ? this.ReadValue(node, where + ".default", type) : null;
        var rootDefault = Has(map, "root_default") ? this.Scalar(map, "root_default", where) : null;
        var range = map.Children.TryGetValue(new YamlScalarNode("range"), out var rangeNode) ? this.ReadRange(rangeNode, where + ".range") : null;
        var commit = Has(map, "commit") ? this.Choice(map, "commit", where, Commits) : null;
        var unit = Has(map, "unit") ? this.Scalar(map, "unit", where) : null;
        var displayUnit = Has(map, "display_unit") ? this.Scalar(map, "display_unit", where) : null;
        return this.diagnostics.Count != count
            ? null
            : new SceneComponentsSchema.ValuesEntry(overrideId, value, rootDefault, range, string.Equals(commit, "clamp", StringComparison.Ordinal), unit, displayUnit);
    }

    private SceneComponentsSchema.ValueEntry? ReadValue(YamlNode node, string where, string? type)
        => type switch
        {
            "vec3" => this.Numbers(node, where) is { } numbers ? new SceneComponentsSchema.ValueEntry(scalar: null, numbers, policy: null) : null,
            "lod-policy" => this.ReadPolicy(node, where) is { } policy ? new SceneComponentsSchema.ValueEntry(scalar: null, numbers: null, policy) : null,
            _ => this.ScalarValue(node, where) is { } scalar ? new SceneComponentsSchema.ValueEntry(scalar, numbers: null, policy: null) : null,
        };

    private SceneComponentsSchema.LodPolicyEntry? ReadPolicy(YamlNode node, string where)
    {
        if (this.Mapping(node, where) is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, where, ["type"], PolicyKeys);
        var type = this.Scalar(map, "type", where);
        var lodIndex = Has(map, "lod_index") ? this.Integer(map, "lod_index", where) : 0;
        var thresholds = this.OptionalNumbers(map, "thresholds", where);
        var hysteresis = Has(map, "hysteresis_ratio") ? this.Number(map.Children[new YamlScalarNode("hysteresis_ratio")], where + ".hysteresis_ratio") : 0;
        var enter = this.OptionalNumbers(map, "enter_finer_sse", where);
        var exit = this.OptionalNumbers(map, "exit_coarser_sse", where);
        return type is null || lodIndex is null || thresholds is null || hysteresis is null || enter is null || exit is null
            ? null
            : new SceneComponentsSchema.LodPolicyEntry(type, lodIndex.Value, thresholds, hysteresis.Value, enter, exit);
    }

    private SceneComponentsSchema.RangeEntry? ReadRange(YamlNode node, string where)
    {
        if (this.Mapping(node, where) is not YamlMappingNode map)
        {
            return null;
        }

        this.CheckKeys(map, where, [], RangeKeys);
        var min = map.Children.TryGetValue(new YamlScalarNode("min"), out var minNode) ? this.Number(minNode, where + ".min") : null;
        var max = map.Children.TryGetValue(new YamlScalarNode("max"), out var maxNode) ? this.Number(maxNode, where + ".max") : null;
        return new SceneComponentsSchema.RangeEntry(
            min,
            max,
            this.Flag(map, "min_exclusive", where),
            this.Flag(map, "max_exclusive", where),
            this.Flag(map, "nonzero", where));
    }

    private List<double>? OptionalNumbers(YamlMappingNode map, string key, string where)
        => map.Children.TryGetValue(new YamlScalarNode(key), out var node) ? this.Numbers(node, where + "." + key) : [];

    private List<double>? Numbers(YamlNode node, string where)
    {
        if (node is not YamlSequenceNode sequence)
        {
            this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(node), $"{where} must be a sequence of numbers");
            return null;
        }

        var numbers = sequence.Children.Select(item => this.Number(item, where)).ToList();
        return numbers.TrueForAll(static number => number is not null) ? numbers.ConvertAll(static number => number!.Value) : null;
    }

    private double? Number(YamlNode node, string where)
    {
        if (this.ScalarValue(node, where) is not string text)
        {
            return null;
        }

        if (double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var value))
        {
            return value;
        }

        this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(node), $"{where} must be a number, not '{text}'");
        return null;
    }

    private bool Flag(YamlMappingNode map, string key, string where)
    {
        if (this.Scalar(map, key, where) is not string text)
        {
            return false;
        }

        if (bool.TryParse(text, out var value))
        {
            return value;
        }

        this.Report(SceneComponentsDiagnostics.InvalidShape, this.At(map.Children[new YamlScalarNode(key)]), $"{where}.{key} must be true or false, not '{text}'");
        return false;
    }
}
