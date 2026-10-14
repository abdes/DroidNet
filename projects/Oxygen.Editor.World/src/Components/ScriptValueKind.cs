// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World;

/// <summary>
/// Type of a script parameter value set on a slot.
/// </summary>
public enum ScriptValueKind
{
    /// <summary>A boolean.</summary>
    Bool,

    /// <summary>A 32-bit integer.</summary>
    Int32,

    /// <summary>A 32-bit float.</summary>
    Float,

    /// <summary>A string.</summary>
    String,

    /// <summary>Two floats.</summary>
    Vec2,

    /// <summary>Three floats.</summary>
    Vec3,

    /// <summary>Four floats.</summary>
    Vec4,

    /// <summary>A reference to an asset.</summary>
    Asset,

    /// <summary>A reference to a node of the same scene.</summary>
    Node,
}
