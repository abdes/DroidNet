// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Components;

/// <summary>A component a node cannot take a second one of, and the node that already has one of its family.</summary>
/// <param name="Node">The node.</param>
/// <param name="Component">The component to add.</param>
public sealed record DuplicateCase(NodeId Node, ComponentData Component);
