// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Type of a script parameter value set on a slot.
/// </summary>
public enum ScriptValueKind
{
    /// <summary>A boolean.</summary>
    BoolValue,

    /// <summary>A 32-bit integer.</summary>
    Int32Value,

    /// <summary>A 32-bit float.</summary>
    FloatValue,

    /// <summary>A string.</summary>
    StringValue,

    /// <summary>Two floats.</summary>
    Vec2Value,

    /// <summary>Three floats.</summary>
    Vec3Value,

    /// <summary>Four floats.</summary>
    Vec4Value,

    /// <summary>A reference to an asset.</summary>
    AssetValue,

    /// <summary>A reference to a node of the same scene.</summary>
    NodeValue,
}
