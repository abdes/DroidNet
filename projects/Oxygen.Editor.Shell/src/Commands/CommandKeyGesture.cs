// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Windows.System;

namespace Oxygen.Editor.Shell.Commands;

/// <summary>A key, with its modifiers, that runs a command.</summary>
/// <param name="Key">The key.</param>
/// <param name="Modifiers">The modifier keys held with it.</param>
public sealed record CommandKeyGesture(VirtualKey Key, VirtualKeyModifiers Modifiers);
