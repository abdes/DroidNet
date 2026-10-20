// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scripting;

/// <summary>
/// Declared type of a script parameter.
/// </summary>
public enum ScriptParameterKind
{
    /// <summary>No type; not produced for valid declarations.</summary>
    None,

    /// <summary>A boolean.</summary>
    BoolParameter,

    /// <summary>A 32-bit integer.</summary>
    Int32Parameter,

    /// <summary>A 32-bit float.</summary>
    FloatParameter,

    /// <summary>A string.</summary>
    StringParameter,

    /// <summary>Two floats.</summary>
    Vec2Parameter,

    /// <summary>Three floats.</summary>
    Vec3Parameter,

    /// <summary>Four floats.</summary>
    Vec4Parameter,

    /// <summary>A reference to an asset of a declared kind.</summary>
    AssetParameter,

    /// <summary>A reference to a node of the scene.</summary>
    NodeParameter,
}
