// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// One value a scene stores over a component's sources.
/// </summary>
/// <param name="Property">The overridden property.</param>
/// <param name="Part">The part the value belongs to, or <see langword="null"/> for a property of the component itself.</param>
/// <param name="LayoutRevision">The revision of the part layout the part was witnessed in, or <see langword="null"/> without a part.</param>
/// <param name="Value">The value, of the property's value type.</param>
public readonly record struct SourceOverride(PropertyId Property, PartId? Part, string? LayoutRevision, object Value);
