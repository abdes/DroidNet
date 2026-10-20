// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// One parameter a script declares, with its authoring hints. A script read from its source and one read from a cooked
/// library declare their parameters in this one vocabulary; a cooked script keeps no hints.
/// </summary>
public sealed record ScriptParameter
{
    /// <summary>Gets the parameter name.</summary>
    public required string Name { get; init; }

    /// <summary>Gets the declared type.</summary>
    public required ScriptParameterKind Kind { get; init; }

    /// <summary>Gets the declared default; <see langword="null"/> for asset and node parameters, which default to
    /// none.</summary>
    public ScriptValue? Default { get; init; }

    /// <summary>Gets the native asset type an asset parameter accepts; 0 otherwise.</summary>
    public byte AssetType { get; init; }

    /// <summary>Gets the minimum, if any.</summary>
    public double? Min { get; init; }

    /// <summary>Gets the maximum, if any.</summary>
    public double? Max { get; init; }

    /// <summary>Gets the step, if any.</summary>
    public double? Step { get; init; }

    /// <summary>Gets the unit label, or empty.</summary>
    public string Unit { get; init; } = string.Empty;

    /// <summary>Gets the description, or empty.</summary>
    public string Description { get; init; } = string.Empty;

    /// <summary>Gets the allowed choices of a string parameter, or empty.</summary>
    public IReadOnlyList<string> Options { get; init; } = [];

    /// <summary>Gets a value indicating whether a vector is a color.</summary>
    public bool IsColor { get; init; }

    /// <summary>Gets the type of the value a script slot stores for the parameter; <see langword="null"/> when the
    /// declaration has no type.</summary>
    public ScriptValueKind? ValueKind => this.Kind switch
    {
        ScriptParameterKind.BoolParameter => ScriptValueKind.BoolValue,
        ScriptParameterKind.Int32Parameter => ScriptValueKind.Int32Value,
        ScriptParameterKind.FloatParameter => ScriptValueKind.FloatValue,
        ScriptParameterKind.StringParameter => ScriptValueKind.StringValue,
        ScriptParameterKind.Vec2Parameter => ScriptValueKind.Vec2Value,
        ScriptParameterKind.Vec3Parameter => ScriptValueKind.Vec3Value,
        ScriptParameterKind.Vec4Parameter => ScriptValueKind.Vec4Value,
        ScriptParameterKind.AssetParameter => ScriptValueKind.AssetValue,
        ScriptParameterKind.NodeParameter => ScriptValueKind.NodeValue,
        _ => null,
    };
}
