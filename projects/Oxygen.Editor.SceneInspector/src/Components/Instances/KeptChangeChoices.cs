// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Nodes;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>What <i>Reassign…</i> offers for one kept change: where it repairs it, and the nodes to choose from.</summary>
/// <param name="Row">The kept change.</param>
/// <param name="Repair">Where the change is repaired.</param>
/// <param name="Scope">What the repair reaches, for the user: the model in every scene, or this instance only.</param>
/// <param name="Targets">The nodes, those new in the model's last import first.</param>
public sealed record KeptChangeChoices(KeptChangeRow Row, KeptChangeRepair Repair, string Scope, IReadOnlyList<KeptChangeTarget> Targets);
