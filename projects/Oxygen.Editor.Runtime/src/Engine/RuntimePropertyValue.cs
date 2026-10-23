// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>One engine field of a component property at the managed runtime boundary.</summary>
/// <param name="ComponentId">The component the property belongs to.</param>
/// <param name="FieldId">The component-local field, a value of that component's Interop field enumeration.</param>
/// <param name="Value">The field's value, of the field's schema value type, in engine units.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimePropertyValue(RuntimeComponentId ComponentId, ushort FieldId, RuntimeFieldValue Value);
