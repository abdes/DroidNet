// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>A node an instance brought in and the owned node Unpack made of it.</summary>
/// <param name="Path">The node's path below the instance root.</param>
/// <param name="Copy">The owned node's ID.</param>
public sealed record UnpackedNode(NodePath Path, NodeId Copy);
