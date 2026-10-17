// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>The kinds of light node.</summary>
public enum LightKind
{
    /// <summary>A directional light, pointing down at its default angle.</summary>
    Directional = 0,

    /// <summary>A point light.</summary>
    Point = 1,

    /// <summary>A spot light.</summary>
    Spot = 2,
}
