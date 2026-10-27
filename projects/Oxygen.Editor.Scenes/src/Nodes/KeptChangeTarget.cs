// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>A node a kept change can be reassigned to.</summary>
/// <param name="Path">The node's path below the instance root.</param>
/// <param name="Names">The node's names: <c>Body/Door_Front_L</c>.</param>
/// <param name="IsNew">Whether the model's last import reported the node as new.</param>
/// <param name="Refusal">Why the node cannot be chosen, or <see langword="null"/> when it can.</param>
/// <param name="Kept">How many values this scene changes of the node under its own ID, which become kept changes.</param>
public sealed record KeptChangeTarget(NodePath Path, string Names, bool IsNew, string? Refusal, int Kept);
