// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed Key input intent.</summary>
/// <param name="Key">The Key input value.</param>
/// <param name="Pressed">The Pressed input value.</param>
/// <param name="Repeat">The Repeat input value.</param>
/// <param name="Position">The Position input value.</param>
/// <param name="Timestamp">The Timestamp input value.</param>
public sealed record RuntimeKeyEvent(RuntimeKey Key, bool Pressed, bool Repeat, Vector2 Position, DateTime Timestamp) : RuntimeInputEvent;
