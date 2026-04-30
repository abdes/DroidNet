// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.World.Messages;

/// <summary>
/// Snapshot of geometry selection for messaging and undo/redo purposes.
/// </summary>
/// <param name="Uri">The geometry URI, or <see langword="null"/> when no geometry is set.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct GeometrySnapshot(Uri? Uri);
