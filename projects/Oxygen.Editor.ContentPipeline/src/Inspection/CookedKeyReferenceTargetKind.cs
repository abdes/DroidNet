// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Stable key-reference target kinds emitted by the native Inspector.</summary>
public enum CookedKeyReferenceTargetKind
{
    /// <summary>Not a native value: the report refuses it.</summary>
    None = 0,

    /// <summary>A keyed asset in a cooked container.</summary>
    Asset = 1,

    /// <summary>A physics resource.</summary>
    PhysicsResource = 2,

    /// <summary>A logical key that names no stored asset.</summary>
    Logical = 3,

    /// <summary>A texture another container holds, which an external texture binding names.</summary>
    Texture = 4,
}
