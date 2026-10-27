// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>Where a kept change is repaired (scene instances SI-16).</summary>
public enum KeptChangeRepair
{
    /// <summary>The change's node path is rewritten to the chosen node: an edit of this scene only.</summary>
    Instance,

    /// <summary>
    /// The model's import settings record that the chosen node continues the lost one, and the model is reimported: the
    /// chosen node takes the lost node's ID, so the change applies to it in every scene.
    /// </summary>
    Model,
}
