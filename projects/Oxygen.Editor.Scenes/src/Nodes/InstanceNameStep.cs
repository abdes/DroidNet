// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>One step of a name path: a node's name and its order among the siblings that have that name.</summary>
/// <param name="Name">The node's name.</param>
/// <param name="Ordinal">How many earlier siblings have the name: 0 for the first.</param>
public readonly record struct InstanceNameStep(string Name, int Ordinal);
