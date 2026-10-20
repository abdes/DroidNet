// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Settings;

/// <summary>Replaces the input mapping contexts that are active when the scene starts.</summary>
/// <remarks>Nothing reaches the engine: editor viewports keep their own input.</remarks>
public sealed record SetStartingInputContexts : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="SetStartingInputContexts"/> class.</summary>
    /// <param name="contexts">The complete list, in order; the edit keeps its own copy.</param>
    public SetStartingInputContexts(IReadOnlyList<StartingInputContextData> contexts)
    {
        ArgumentNullException.ThrowIfNull(contexts);
        this.Contexts = [.. contexts];
    }

    /// <summary>Gets the contexts the scene starts with, in order.</summary>
    public IReadOnlyList<StartingInputContextData> Contexts { get; }

    /// <inheritdoc/>
    public string Label => "Edit Starting Input Contexts";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // The contexts as given are checked first: following moved assets needs well-formed entries.
        return (Check(this.Contexts) ?? Check(this.Resolved(scope))) is { } error
            ? SceneEditValidation.Invalid(error)
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var before = scope.Scene.StartingInputContexts;
        var contexts = this.Resolved(scope);
        if (before.SequenceEqual(contexts))
        {
            return new SceneEditApplication.Applied(this, []);
        }

        scope.WarnIfDeleted(contexts.Select(static entry => (AssetPath?)entry.Context));
        scope.Scene.SetStartingInputContexts(contexts);
        return new SceneEditApplication.Applied(new SetStartingInputContexts(before), [new SceneChange.StartingInputContextsChanged()]);
    }

    private static SceneDiagnostic? Check(IReadOnlyList<StartingInputContextData> contexts)
    {
        if (contexts.Any(static entry => entry is null))
        {
            return SceneDiagnostic.Error(SceneEditCodes.EntryMissing, "The starting input context list is missing an entry.");
        }

        if (AssetReferenceRules.Check(contexts.Select(static entry => (AssetPath?)entry.Context), AssetFileTypes.InputMappingContext) is { } invalid)
        {
            return SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, invalid);
        }

        return contexts.Select(static entry
            => entry.Context).Distinct().Take(contexts.Count + 1).Count() != contexts.Count
                ? SceneDiagnostic.Error(SceneEditCodes.EntryDuplicate, "An input context can start only once.")
                : null;
    }

    private IReadOnlyList<StartingInputContextData> Resolved(SceneEditScope scope)
        => SceneReferenceRetargeting.Retarget(this.Contexts, scope.Moved) ?? this.Contexts;
}
