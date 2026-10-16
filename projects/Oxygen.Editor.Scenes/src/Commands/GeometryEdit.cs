// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Partial edit for a geometry component.
/// </summary>
/// <param name="GeometryUri">Optional geometry asset URI. A supplied null is rejected because geometry components must remain savable.</param>
public sealed record GeometryEdit(OptionalEditValue<Uri?> GeometryUri);
