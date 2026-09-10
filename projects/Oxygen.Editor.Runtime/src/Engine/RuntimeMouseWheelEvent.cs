// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed MouseWheel input intent.</summary>
/// <param name="Scroll">The Scroll input value.</param>
/// <param name="Position">The Position input value.</param>
/// <param name="Timestamp">The Timestamp input value.</param>
public sealed record RuntimeMouseWheelEvent(Vector2 Scroll, Vector2 Position, DateTime Timestamp) : RuntimeInputEvent;
