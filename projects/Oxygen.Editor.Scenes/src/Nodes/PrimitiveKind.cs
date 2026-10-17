// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>The built-in shapes a primitive node can show.</summary>
public enum PrimitiveKind
{
    /// <summary>A sphere.</summary>
    Sphere = 0,

    /// <summary>A cube.</summary>
    Cube = 1,

    /// <summary>A cylinder.</summary>
    Cylinder = 2,

    /// <summary>A cone.</summary>
    Cone = 3,

    /// <summary>A plane.</summary>
    Plane = 4,

    /// <summary>A capsule.</summary>
    Capsule = 5,

    /// <summary>An icosphere.</summary>
    IcoSphere = 6,

    /// <summary>A torus.</summary>
    Torus = 7,

    /// <summary>A quad.</summary>
    Quad = 8,

    /// <summary>A subdivided cube.</summary>
    SubdividedCube = 9,
}
