// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets;

/// <summary>The engine's asset types, as cooked indexes and descriptors record them.</summary>
/// <remarks>Mirrors <c>oxygen::data::AssetType</c> (<c>Oxygen/Core/Meta/Data/AssetType.inc</c>); the values are part of the cooked format.</remarks>
public enum AssetType : byte
{
    /// <summary>Not an asset type the engine knows.</summary>
    Unknown = 0,

    /// <summary>A material.</summary>
    Material = 1,

    /// <summary>A geometry.</summary>
    Geometry = 2,

    /// <summary>A scene.</summary>
    Scene = 3,

    /// <summary>A script.</summary>
    Script = 4,

    /// <summary>An input action.</summary>
    InputAction = 5,

    /// <summary>An input mapping context.</summary>
    InputMappingContext = 6,

    /// <summary>A physics material.</summary>
    PhysicsMaterial = 7,

    /// <summary>A collision shape.</summary>
    CollisionShape = 8,

    /// <summary>A physics scene sidecar.</summary>
    PhysicsScene = 9,
}
