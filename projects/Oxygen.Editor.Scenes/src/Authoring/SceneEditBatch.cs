// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>Several edits that apply as one: all of them, in order, or none.</summary>
/// <remarks>
/// <para>
/// Each inner edit is checked when it runs, against the scene as the previous edits left it, which lets a batch create a
/// node and then edit it. When one is refused or rejected, the reverts of those already applied run in reverse order and
/// the batch is rejected: the scene is as before, nothing is recorded and no change is raised.
/// </para>
/// <para>
/// Validating a batch runs it on a scratch copy of the scene; execution relies on the rollback instead.
/// </para>
/// </remarks>
/// <param name="Label">The batch's name in the history, as people read it.</param>
/// <param name="Edits">The edits, in the order they apply.</param>
public sealed record SceneEditBatch(string Label, IReadOnlyList<ISceneEdit> Edits) : ISceneEdit
{
    /// <inheritdoc/>
    /// <remarks>Empty: each inner edit's targets are checked when it runs.</remarks>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    public bool ChangesDocument => this.Edits.Any(static edit => edit.ChangesDocument);

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        return ((ISceneEdit)this).Apply(scope.On(Scratch(scope.Scene))) is SceneEditApplication.Rejected rejected
            ? rejected.Validation
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Edits.Count == 0)
        {
            return new SceneEditApplication.Rejected(SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.BatchEmpty, "The batch has no edits.")));
        }

        var reverts = new List<ISceneEdit>(this.Edits.Count);
        var changes = new List<SceneChange>();
        foreach (var edit in this.Edits)
        {
            var validation = scope.CheckTargets(edit);
            if (validation.IsValid && edit is not SceneEditBatch)
            {
                validation = edit.Validate(scope);
            }

            if (!validation.IsValid)
            {
                Rollback(scope, reverts);
                return new SceneEditApplication.Rejected(validation);
            }

            scope.Warn(validation.Diagnostics);
            switch (edit.Apply(scope))
            {
                // An edit that raised no change left the scene as it was; it has nothing to revert.
                case SceneEditApplication.Applied applied when applied.Changes.Count > 0:
                    reverts.Add(applied.Revert);
                    changes.AddRange(applied.Changes);
                    break;
                case SceneEditApplication.Applied:
                    break;
                case SceneEditApplication.Rejected rejected:
                    Rollback(scope, reverts);
                    return rejected;
                default:
                    throw new InvalidOperationException($"'{edit.Label}' returned an unknown application.");
            }
        }

        reverts.Reverse();
        return new SceneEditApplication.Applied(new SceneEditBatch(this.Label, reverts), changes);
    }

    // A copy that shares nothing with the scene, its explorer layout included, so applying to it leaves the scene as is.
    private static Scene Scratch(Scene scene)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(scene.Dehydrate(), SceneJsonContext.Default.SceneData);
        var data = JsonSerializer.Deserialize(bytes, SceneJsonContext.Default.SceneData)!;
        var scratch = Scene.CreateAndHydrate(scene.Project, data);
        scratch.SetExplorerLayout(data.ExplorerLayout);
        return scratch;
    }

    // An exact inverse cannot be refused on the state its edit left, so a refused revert is a defect.
    private static void Rollback(SceneEditScope scope, List<ISceneEdit> reverts)
    {
        for (var i = reverts.Count - 1; i >= 0; i--)
        {
            if (reverts[i].Apply(scope) is not SceneEditApplication.Applied)
            {
                throw new InvalidOperationException($"The revert of '{reverts[i].Label}' was rejected while a batch rolled back.");
            }
        }
    }
}
