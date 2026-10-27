// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>One property of a node an instance brings in, and its effective value.</summary>
/// <param name="Property">The property.</param>
/// <param name="Part">The surface of a surface property; <see cref="Guid.Empty"/> otherwise.</param>
/// <param name="LayoutRevision">The geometry layout revision of a surface property; <see langword="null"/> otherwise.</param>
/// <param name="Value">The value, of the type <see cref="SceneSchema.Changeable"/> gives the property.</param>
public sealed record InstancedValue(PropertyId Property, Guid Part, string? LayoutRevision, object Value);
