// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine.Changes;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>A scene's history entry: the edit that undoes, or redoes, what an earlier edit did, named after that edit.</summary>
/// <remarks>
/// Applying it replays the edit through the scene's authoring, which records the edit's revert as the inverse; the history
/// routes that inverse to the other stack. An entry that cannot replay throws <see cref="SceneReplayRefusedException"/>
/// before recording anything, so the history keeps it.
/// </remarks>
/// <param name="authoring">The scene's authoring.</param>
/// <param name="edit">The edit to replay.</param>
/// <param name="label">The name of the edit people made, which the entry keeps through undo and redo.</param>
internal sealed class SceneHistoryEntry(SceneAuthoring authoring, ISceneEdit edit, string label) : IChange
{
    /// <summary>Gets the edit the entry replays.</summary>
    public ISceneEdit Edit { get; } = edit;

    /// <summary>Gets the name of the edit people made.</summary>
    public string Label { get; } = label;

    /// <inheritdoc/>
    public object Key => this.Label;

    /// <inheritdoc/>
    public bool ChangesState => this.Edit.ChangesDocument;

    /// <inheritdoc/>
    public void Apply() => authoring.Replay(this.Edit, this.Label);

    /// <inheritdoc/>
    public override string ToString() => this.Label;
}
