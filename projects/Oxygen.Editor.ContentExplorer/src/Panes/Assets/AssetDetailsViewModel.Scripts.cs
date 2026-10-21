// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets;

/// <summary>
/// Script and input facts of the details pane: declared parameters, problem counts and the declaring input file.
/// </summary>
public sealed partial class AssetDetailsViewModel
{
    /// <summary>Builds the facts the analysis of a script adds to its details.</summary>
    /// <param name="script">The analyzed script, or <see langword="null"/> while it has not been analyzed.</param>
    /// <returns>The parameter and problem facts.</returns>
    public static IReadOnlyList<AssetInformationFact> BuildScriptFacts(Script? script)
    {
        if (script is null)
        {
            return [new("Problems", "Not analyzed yet")];
        }

        var facts = new List<AssetInformationFact>
        {
            new("Problems", script.Problems.Count == 0 ? "None" : script.ProblemSummary),
        };
        if (script.Parameters.Count > 0)
        {
            var label = script.ParametersOutOfDate ? "Parameters (out of date)" : "Parameters";
            facts.Add(new(label, string.Join(Environment.NewLine, script.Parameters.Select(Describe))));
        }

        return facts;
    }

    private static string Describe(ScriptParameter parameter)
    {
        var type = parameter.Kind switch
        {
            ScriptParameterKind.Vec3Parameter or ScriptParameterKind.Vec4Parameter when parameter.IsColor => "color",
            ScriptParameterKind.AssetParameter => "asset (" + AssetKinds.FromCookedType(parameter.AssetType) + ")",
            _ => parameter.Kind.ToString().ToLowerInvariant(),
        };
        var value = parameter.Default switch
        {
            null => null,
            { Kind: ScriptValueKind.BoolValue } flag => flag.BoolValue ? "true" : "false",
            { Kind: ScriptValueKind.Int32Value } number => number.IntValue.ToString(CultureInfo.CurrentCulture),
            { Kind: ScriptValueKind.FloatValue } number => number.FloatValue.ToString("0.###", CultureInfo.CurrentCulture),
            { Kind: ScriptValueKind.StringValue } text => "\"" + text.StringValue + "\"",
            { Kind: ScriptValueKind.Vec2Value or ScriptValueKind.Vec3Value or ScriptValueKind.Vec4Value } vector
                => "(" + string.Join(", ", vector.GetVectorComponents().Select(static component => component.ToString("0.###", CultureInfo.CurrentCulture))) + ")",
            var other => other.ToString(),
        };
        var unit = parameter.Unit.Length > 0 ? " " + parameter.Unit : string.Empty;
        return value is null ? $"{parameter.Name}: {type}" : $"{parameter.Name}: {type} = {value}{unit}";
    }

    private IEnumerable<AssetInformationFact> BuildSourceFacts(AssetEntry asset)
    {
        if (asset is { Kind: AssetKind.Script } && this.scripts is not null)
        {
            return BuildScriptFacts(this.scripts.Find(AssetFileTypes.ToIdentity(asset.Identity)));
        }

        return [];
    }

    private void OnScriptChanged(ScriptChangedEventArgs args)
    {
        if (!this.disposed && this.Asset is { Kind: AssetKind.Script } asset
            && (args.Script is null || AssetFileTypes.ToIdentity(asset.Identity) == args.Script))
        {
            this.Refresh();
        }
    }
}
