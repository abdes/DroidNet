// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>
/// One engine field value for the property-pipeline transport.
/// Mirrors <c>Oxygen::Interop::World::PropertyValueEntry</c>.
/// </summary>
/// <remarks>
/// The <paramref name="FieldId"/> is component-local; its meaning is only
/// valid in the context of <paramref name="Component"/>. The value is of the
/// field's schema value type; vectors and colors go as one float field per axis.
/// </remarks>
/// <param name="Component">The target component class.</param>
/// <param name="FieldId">The component-local field id.</param>
/// <param name="Value">The field value.</param>
[StructLayout(LayoutKind.Auto)]
internal readonly record struct EnginePropertyValueEntry(
    RuntimeComponentId Component,
    ushort FieldId,
    RuntimeFieldValue Value);
