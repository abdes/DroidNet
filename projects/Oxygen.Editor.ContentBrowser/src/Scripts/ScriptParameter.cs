// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// One parameter a script declares, with its authoring hints.
/// </summary>
/// <param name="Name">The parameter name.</param>
/// <param name="Kind">The declared type.</param>
/// <param name="DefaultValue">The default: bool, int, float, string, Vector2, Vector3 or Vector4; null for asset and
/// node parameters.</param>
/// <param name="AssetType">The native asset type of an asset parameter; 0 otherwise.</param>
/// <param name="Min">The minimum, if any.</param>
/// <param name="Max">The maximum, if any.</param>
/// <param name="Step">The step, if any.</param>
/// <param name="Unit">The unit label, or empty.</param>
/// <param name="Description">The description, or empty.</param>
/// <param name="Options">The allowed choices, or empty.</param>
/// <param name="IsColor">Whether a vector is a color.</param>
public sealed record ScriptParameter(
    string Name,
    ScriptParameterKind Kind,
    object? DefaultValue,
    byte AssetType,
    double? Min,
    double? Max,
    double? Step,
    string Unit,
    string Description,
    IReadOnlyList<string> Options,
    bool IsColor);
