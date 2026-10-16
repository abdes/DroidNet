// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
///     An edit gesture in progress on a material: the source before it began, and how it ends.
/// </summary>
/// <param name="session">The gesture's session, which its previews and its end name.</param>
/// <param name="label">The history entry's label when the gesture commits.</param>
/// <param name="before">The source before the gesture began.</param>
/// <param name="finish">Ends the gesture, committing its last preview as one history entry or cancelling it.</param>
internal sealed class MaterialGesture(MaterialEditSession session, string label, MaterialSource before, Action<bool> finish)
{
    /// <summary>Gets the gesture's session.</summary>
    public MaterialEditSession Session { get; } = session;

    /// <summary>Gets the history entry's label when the gesture commits.</summary>
    public string Label { get; } = label;

    /// <summary>Gets the source before the gesture began.</summary>
    public MaterialSource Before { get; } = before;

    /// <summary>Ends the gesture.</summary>
    /// <param name="commit">Whether to keep the gesture's last preview as one history entry.</param>
    public void Finish(bool commit) => finish(commit);
}
