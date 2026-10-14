// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.ContentPipeline.Inspection;

namespace Oxygen.Editor.World.Inspector.Scripting;

/// <summary>Where the parameters shown for a slot's script come from.</summary>
internal enum ScriptDeclarationOrigin
{
    /// <summary>Not known: no script is picked, or its source has not been analyzed yet.</summary>
    Unknown,

    /// <summary>The script's source in the project, through script analysis.</summary>
    Source,

    /// <summary>A cooked script from a mounted library, which has no source in the project.</summary>
    Library,

    /// <summary>A library script whose declarations are being read.</summary>
    LibraryLoading,

    /// <summary>A library script whose declarations could not be read.</summary>
    LibraryUnavailable,
}

/// <summary>The parameters a slot's script declares, and where they come from.</summary>
/// <param name="Origin">Where the declarations come from.</param>
/// <param name="Parameters">The declared parameters; empty when they are not known.</param>
/// <param name="Analysis">The source analysis, with its problems, for a script with a source.</param>
/// <param name="Failure">Why a library script's declarations could not be read.</param>
internal sealed record ScriptDeclarations(
    ScriptDeclarationOrigin Origin,
    IReadOnlyList<ScriptParameter> Parameters,
    ScriptAnalysisSnapshot? Analysis = null,
    string Failure = "")
{
    /// <summary>Gets the declarations of a script that is not known.</summary>
    public static ScriptDeclarations Unknown { get; } = new(ScriptDeclarationOrigin.Unknown, []);

    /// <summary>Gets the declarations of a library script that is being read.</summary>
    public static ScriptDeclarations LibraryLoading { get; } = new(ScriptDeclarationOrigin.LibraryLoading, []);

    /// <summary>Gets a value indicating whether the parameters are known, so values can be judged against them.</summary>
    public bool AreKnown => this.Origin is ScriptDeclarationOrigin.Source or ScriptDeclarationOrigin.Library;

    /// <summary>Gets the declarations from a script's source analysis.</summary>
    /// <param name="analysis">The analysis.</param>
    /// <returns>The declarations.</returns>
    public static ScriptDeclarations FromSource(ScriptAnalysisSnapshot analysis)
        => new(ScriptDeclarationOrigin.Source, analysis.Parameters, analysis);

    /// <summary>Gets the declarations a cooked library script records.</summary>
    /// <param name="cooked">The cooked declarations.</param>
    /// <returns>The declarations; cooked scripts carry no range, unit, description, options or color.</returns>
    public static ScriptDeclarations FromLibrary(CookedScriptDeclarations cooked)
        => new(ScriptDeclarationOrigin.Library, [.. cooked.Parameters.Select(static parameter => new ScriptParameter(
            parameter.Name,
            ToKind(parameter.Kind),
            parameter.DefaultValue,
            parameter.AssetType,
            Min: null,
            Max: null,
            Step: null,
            Unit: string.Empty,
            Description: string.Empty,
            Options: ImmutableArray<string>.Empty,
            IsColor: false))]);

    /// <summary>Gets the declarations of a library script that could not be read.</summary>
    /// <param name="reason">Why.</param>
    /// <returns>The declarations.</returns>
    public static ScriptDeclarations LibraryUnavailable(string reason)
        => new(ScriptDeclarationOrigin.LibraryUnavailable, [], Failure: reason);

    private static ScriptParameterKind ToKind(CookedScriptParameterKind kind)
        => kind switch
        {
            CookedScriptParameterKind.Bool => ScriptParameterKind.Bool,
            CookedScriptParameterKind.Int32 => ScriptParameterKind.Int32,
            CookedScriptParameterKind.Float => ScriptParameterKind.Float,
            CookedScriptParameterKind.String => ScriptParameterKind.String,
            CookedScriptParameterKind.Vec2 => ScriptParameterKind.Vec2,
            CookedScriptParameterKind.Vec3 => ScriptParameterKind.Vec3,
            CookedScriptParameterKind.Vec4 => ScriptParameterKind.Vec4,
            CookedScriptParameterKind.Asset => ScriptParameterKind.Asset,
            CookedScriptParameterKind.Node => ScriptParameterKind.Node,
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unknown cooked script parameter type."),
        };
}
