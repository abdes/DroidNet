// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Stable wire identifier for the component class an editor property
/// targets. Mirrors <c>oxygen::interop::module::ComponentId</c>.
/// </summary>
/// <remarks>
/// <para>
/// This is the *coarse* dimension of the property-pipeline §5.3 wire
/// vocabulary. The fine dimension — a component-local field id — is
/// owned by the matching native applier class and never appears here.
/// </para>
/// <para>
/// Adding a new component class is one new entry here, one new
/// applier on the native side, and one registration call. Adding a
/// new property to an existing component touches neither this enum
/// nor the transport.
/// </para>
/// </remarks>
public enum EngineComponentId : ushort
{
    /// <summary>Sentinel; not a valid wire id.</summary>
    Invalid = 0,

    /// <summary><c>oxygen::scene::TransformComponent</c>.</summary>
    Transform = 1,

    // Future: Material = 2, Renderable = 3, ...
}

/// <summary>
/// Component-local field ids for <see cref="EngineComponentId.Transform"/>.
/// Mirrors <c>oxygen::interop::module::TransformField</c>.
/// </summary>
public enum TransformField : ushort
{
    /// <summary>Local position X.</summary>
    PositionX = 0,

    /// <summary>Local position Y.</summary>
    PositionY = 1,

    /// <summary>Local position Z.</summary>
    PositionZ = 2,

    /// <summary>Local rotation X (Euler degrees).</summary>
    RotationXDegrees = 3,

    /// <summary>Local rotation Y (Euler degrees).</summary>
    RotationYDegrees = 4,

    /// <summary>Local rotation Z (Euler degrees).</summary>
    RotationZDegrees = 5,

    /// <summary>Local scale X.</summary>
    ScaleX = 6,

    /// <summary>Local scale Y.</summary>
    ScaleY = 7,

    /// <summary>Local scale Z.</summary>
    ScaleZ = 8,
}

/// <summary>
/// Scalar wire entry for the property-pipeline §5.3 transport. Mirrors
/// <c>Oxygen::Interop::World::PropertyValueEntry</c>.
/// </summary>
/// <remarks>
/// The <paramref name="FieldId"/> is component-local; its meaning is
/// only valid in the context of <paramref name="Component"/>. The
/// scalar payload covers every initial use case; vectors, colors and
/// quaternions decompose into per-axis floats so the transport
/// remains uniform.
/// </remarks>
/// <param name="Component">The target component class.</param>
/// <param name="FieldId">The component-local field id.</param>
/// <param name="Value">The scalar value.</param>
public readonly record struct EnginePropertyValueEntry(
    EngineComponentId Component,
    ushort FieldId,
    float Value);
