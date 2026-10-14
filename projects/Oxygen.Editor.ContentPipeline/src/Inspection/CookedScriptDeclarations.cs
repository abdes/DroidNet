// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>The parameters a cooked script declares, as its native descriptor records them.</summary>
/// <remarks>
/// A cooked script keeps each parameter's name, type, default value and asset kind; authoring hints such as range,
/// unit, description, options and color stay in the source and are not cooked.
/// </remarks>
/// <param name="ScriptUri">The requested script identity.</param>
/// <param name="NativeScriptKey">The native asset key.</param>
/// <param name="Parameters">The declared parameters, in declaration order.</param>
public sealed record CookedScriptDeclarations(Uri ScriptUri, Guid NativeScriptKey, ImmutableArray<CookedScriptParameter> Parameters);

/// <summary>One parameter a cooked script declares.</summary>
/// <param name="Name">The parameter name.</param>
/// <param name="Kind">The declared type.</param>
/// <param name="DefaultValue">The default: bool, int, float, string, Vector2, Vector3 or Vector4; null for asset and node
/// parameters.</param>
/// <param name="AssetType">The native asset type an asset parameter accepts, 0 otherwise.</param>
public sealed record CookedScriptParameter(string Name, CookedScriptParameterKind Kind, object? DefaultValue, byte AssetType);

/// <summary>The type of a cooked script parameter.</summary>
public enum CookedScriptParameterKind
{
    /// <summary>A boolean.</summary>
    Bool,

    /// <summary>A 32-bit integer.</summary>
    Int32,

    /// <summary>A 32-bit float.</summary>
    Float,

    /// <summary>A string.</summary>
    String,

    /// <summary>A two-component vector.</summary>
    Vec2,

    /// <summary>A three-component vector.</summary>
    Vec3,

    /// <summary>A four-component vector.</summary>
    Vec4,

    /// <summary>An asset reference.</summary>
    Asset,

    /// <summary>A scene node reference.</summary>
    Node,
}
