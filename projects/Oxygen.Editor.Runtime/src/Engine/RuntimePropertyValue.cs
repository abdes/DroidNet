// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A scalar component property at the managed runtime boundary.</summary>
/// <param name="ComponentId">The stable component identifier.</param>
/// <param name="FieldId">The component-local property identifier.</param>
/// <param name="Value">The authored scalar value in engine units.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimePropertyValue(ushort ComponentId, ushort FieldId, float Value);
