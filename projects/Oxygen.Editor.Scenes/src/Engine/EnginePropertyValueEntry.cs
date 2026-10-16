// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Scalar wire entry for the property-pipeline transport.
/// Mirrors <c>Oxygen::Interop::World::PropertyValueEntry</c>.
/// </summary>
/// <remarks>
/// The <paramref name="FieldId"/> is component-local; its meaning is only
/// valid in the context of <paramref name="Component"/>. The scalar payload
/// covers every initial use case; vectors, colors and quaternions decompose
/// into per-axis floats so the transport remains uniform.
/// </remarks>
/// <param name="Component">The target component class.</param>
/// <param name="FieldId">The component-local field id.</param>
/// <param name="Value">The scalar value.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct EnginePropertyValueEntry(
    EngineComponentId Component,
    ushort FieldId,
    float Value);
