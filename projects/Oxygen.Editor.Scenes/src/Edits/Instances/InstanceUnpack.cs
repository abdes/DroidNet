// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>How deep <see cref="UnpackInstance"/> makes an instance's nodes the scene's own (scene instances 6.8).</summary>
public enum InstanceUnpack
{
    /// <summary>
    /// The source's own nodes, the next nesting level: a node that is itself an instance root stays one, with the changes
    /// below it.
    /// </summary>
    One,

    /// <summary>Every nesting level down to the deepest, so no instance is left.</summary>
    Complete,
}
