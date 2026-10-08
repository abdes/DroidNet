// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A gizmo part the pointer can grab.</summary>
public enum RuntimeGizmoHandle
{
    /// <summary>No handle.</summary>
    None = 0,

    /// <summary>The X arrow, ring or scale handle.</summary>
    X = 1,

    /// <summary>The Y arrow, ring or scale handle.</summary>
    Y = 2,

    /// <summary>The Z arrow, ring or scale handle.</summary>
    Z = 3,

    /// <summary>The plane handle that moves along X and Y.</summary>
    XY = 4,

    /// <summary>The plane handle that moves along X and Z.</summary>
    XZ = 5,

    /// <summary>The plane handle that moves along Y and Z.</summary>
    YZ = 6,

    /// <summary>Translate in the view plane, or scale uniformly.</summary>
    Center = 7,

    /// <summary>Rotate about the view direction.</summary>
    View = 8,
}
