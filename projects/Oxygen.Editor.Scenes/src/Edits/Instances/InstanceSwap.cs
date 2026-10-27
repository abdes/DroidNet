// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>What <see cref="SetInstanceSource"/> does with the instance's changes (scene instances 6.5).</summary>
public enum InstanceSwap
{
    /// <summary>
    /// Each change moves to the node of the new source that has its node's name path in the old one; a change that
    /// finds no node is kept, carried from the old source.
    /// </summary>
    KeepByName,

    /// <summary>Every change of the instance is removed.</summary>
    Discard,

    /// <summary>The instance takes exactly <see cref="SetInstanceSource.Changes"/>, as undoing a swap does.</summary>
    Replace,
}
