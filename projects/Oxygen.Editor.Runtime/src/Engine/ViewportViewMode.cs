// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// What an editor viewport renders: the lit scene or one of its diagnostic views. Viewport state,
/// never authored scene data.
/// </summary>
public enum ViewportViewMode
{
    /// <summary>The fully lit scene.</summary>
    Lit = 0,

    /// <summary>Material base colour without lighting.</summary>
    Unlit = 1,

    /// <summary>Geometry edges only.</summary>
    Wireframe = 2,

    /// <summary>The lit scene with geometry edges over it.</summary>
    LitWireframe = 3,

    /// <summary>Direct light contribution only.</summary>
    DirectLighting = 4,

    /// <summary>Image-based (sky) light contribution only.</summary>
    IndirectLighting = 5,

    /// <summary>World-space surface normals.</summary>
    WorldNormals = 6,

    /// <summary>Material roughness.</summary>
    Roughness = 7,

    /// <summary>Material metalness.</summary>
    Metalness = 8,

    /// <summary>Linear scene depth.</summary>
    LinearDepth = 9,

    /// <summary>Directional light shadowing.</summary>
    ShadowMask = 10,
}
