// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed MouseMotion input intent.</summary>
/// <param name="Motion">The Motion input value.</param>
/// <param name="Position">The Position input value.</param>
/// <param name="Timestamp">The Timestamp input value.</param>
public sealed record RuntimeMouseMotionEvent(Vector2 Motion, Vector2 Position, DateTime Timestamp) : RuntimeInputEvent;
