// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World;

/// <summary>
/// Flags available on a <see cref="SceneNode"/> matching engine-side semantics.
/// </summary>
[Flags]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1711:Identifiers should not have incorrect suffix", Justification = "these are really flags and match the engine vocabulary")]
public enum SceneNodeFlags
{
    /// <summary>The default empty flag set.</summary>
    None = 0,

    /// <summary>Node is visible (editor-local boolean).</summary>
    Visible = 1 << 0,

    /// <summary>Node casts shadows.</summary>
    CastsShadows = 1 << 1,

    /// <summary>Node receives shadows.</summary>
    ReceivesShadows = 1 << 2,

    /// <summary>Node may be selected via ray-casting operations.</summary>
    RayCastingSelectable = 1 << 3,

    /// <summary>Node ignores parent transforms when computing world transform.</summary>
    IgnoreParentTransform = 1 << 4,

    /// <summary>Hint that the node is static (editor/runtime optimization hint).</summary>
    Static = 1 << 5,
}
