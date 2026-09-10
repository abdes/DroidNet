// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed Button input intent.</summary>
/// <param name="Button">The Button input value.</param>
/// <param name="Pressed">The Pressed input value.</param>
/// <param name="Position">The Position input value.</param>
/// <param name="Timestamp">The Timestamp input value.</param>
public sealed record RuntimeButtonEvent(RuntimeMouseButton Button, bool Pressed, Vector2 Position, DateTime Timestamp) : RuntimeInputEvent;
