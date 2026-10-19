// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets;

/// <summary>
/// Script and input facts of the details pane: declared parameters, problem counts and the declaring input file.
/// </summary>
public sealed partial class AssetDetailsViewModel
{
    /// <summary>Builds the facts the analysis of a script adds to its details.</summary>
    /// <param name="analysis">The script's analysis, or <see langword="null"/> while it has not been analyzed.</param>
    /// <returns>The parameter and problem facts.</returns>
    public static IReadOnlyList<AssetInformationFact> BuildScriptFacts(ScriptAnalysisSnapshot? analysis)
    {
        if (analysis is null)
        {
            return [new("Problems", "Not analyzed yet")];
        }

        var facts = new List<AssetInformationFact>
        {
            new("Problems", analysis.Problems.Count == 0 ? "None" : analysis.ProblemSummary),
        };
        if (analysis.Parameters.Count > 0)
        {
            var label = analysis.ParametersOutOfDate ? "Parameters (out of date)" : "Parameters";
            facts.Add(new(label, string.Join(Environment.NewLine, analysis.Parameters.Select(Describe))));
        }

        return facts;
    }

    private static string Describe(ScriptParameter parameter)
    {
        var type = parameter.Kind switch
        {
            ScriptParameterKind.Vec3 or ScriptParameterKind.Vec4 when parameter.IsColor => "color",
            ScriptParameterKind.Asset => "asset (" + AssetKinds.FromCookedType(parameter.AssetType) + ")",
            _ => parameter.Kind.ToString().ToLowerInvariant(),
        };
        var value = parameter.DefaultValue switch
        {
            null => null,
            bool flag => flag ? "true" : "false",
            float number => number.ToString("0.###", CultureInfo.CurrentCulture),
            Vector2 v => string.Create(CultureInfo.CurrentCulture, $"({v.X:0.###}, {v.Y:0.###})"),
            Vector3 v => string.Create(CultureInfo.CurrentCulture, $"({v.X:0.###}, {v.Y:0.###}, {v.Z:0.###})"),
            Vector4 v => string.Create(CultureInfo.CurrentCulture, $"({v.X:0.###}, {v.Y:0.###}, {v.Z:0.###}, {v.W:0.###})"),
            string text => "\"" + text + "\"",
            var other => Convert.ToString(other, CultureInfo.CurrentCulture),
        };
        var unit = parameter.Unit.Length > 0 ? " " + parameter.Unit : string.Empty;
        return value is null ? $"{parameter.Name}: {type}" : $"{parameter.Name}: {type} = {value}{unit}";
    }

    private IEnumerable<AssetInformationFact> BuildSourceFacts(AssetEntry asset)
    {
        if (asset is { Kind: AssetKind.Script, SourcePath: { } source } && this.scripts is not null)
        {
            return BuildScriptFacts(this.scripts.Find(source));
        }

        if (asset.Kind is AssetKind.InputAction or AssetKind.InputMappingContext
            && this.projects?.ActiveProject is { } project
            && InputSourceFiles.FindDeclaration(project, asset.Identity) is { } declaration)
        {
            return [new("Declared in", WithPathBreaks(declaration.File.FullPath) + string.Create(CultureInfo.InvariantCulture, $" (line {declaration.Position.Line})"))];
        }

        return [];
    }

    private void OnScriptAnalyzed(ScriptAnalysisSnapshot analysis)
    {
        if (!this.disposed && this.Asset is { Kind: AssetKind.Script, SourcePath: { } source }
            && source == analysis.SourcePath)
        {
            this.Refresh();
        }
    }
}
