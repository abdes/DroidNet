// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// Declared type of a script parameter.
/// </summary>
public enum ScriptParameterKind
{
    /// <summary>No type; not produced for valid declarations.</summary>
    None,

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

    /// <summary>A reference to an asset of a declared kind.</summary>
    Asset,

    /// <summary>A reference to a node of the scene.</summary>
    Node,
}
