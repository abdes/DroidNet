// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// What <see cref="ApplyToSource"/> and <see cref="RestoreSource"/> share: each replaces an authored scene's file with
/// other bytes and an instance root's changes with others, and each checks that both are still what it expects.
/// </summary>
internal static class SourceSceneWrite
{
    /// <summary>Gets the name of a scene, as the history and messages name it: <c>Watchtower</c>.</summary>
    /// <param name="scene">The scene's asset.</param>
    /// <returns>The name.</returns>
    public static string NameOf(AssetPath scene) => Documents.SceneContent.NameOf(scene.Name);

    /// <summary>Gets why a source is not an authored scene, as <i>Apply to source</i> refuses it.</summary>
    /// <param name="source">The source.</param>
    /// <param name="node">The instance root.</param>
    /// <returns>The refusal, or <see langword="null"/> when the source is an authored scene.</returns>
    public static SceneDiagnostic? RefuseModel(AssetPath source, NodeId? node = null)
        => AssetFileTypes.Is(source.Name, AssetFileCategory.Model)
            ? SceneDiagnostic.Error(
                SceneEditCodes.AssetReferenceInvalid,
                $"{source.Name} is an imported model, not an authored scene: its import makes its nodes and a reimport rewrites them, so changes stay on the instance.",
                node)
            : null;

    /// <summary>Gets the nodes whose changes differ between two sets of changes of an instance root.</summary>
    /// <param name="instance">The instance root.</param>
    /// <param name="before">The changes before.</param>
    /// <param name="after">The changes after.</param>
    /// <returns>Each changed node, by its instance root and path.</returns>
    public static IReadOnlyCollection<NodeReference> Moved(NodeId instance, InstanceChanges before, InstanceChanges after)
        => before is null || after is null
            ? []
            : [.. before.Nodes.Keys.Union(after.Nodes.Keys)
                .Where(path => !before.Of(path).Equals(after.Of(path)))
                .OrderBy(static path => path.ToString(), StringComparer.Ordinal)
                .Select(path => new NodeReference(instance, path))];

    /// <summary>Checks that the instance and the scene's file are as the write expects.</summary>
    /// <param name="scope">The scene and its ports.</param>
    /// <param name="instance">The instance root.</param>
    /// <param name="scene">The authored scene whose file is replaced.</param>
    /// <param name="changes">The changes the instance root holds now.</param>
    /// <param name="expected">The bytes the scene's file holds now.</param>
    /// <param name="replacement">The bytes that replace them, which only a deserialized write can lack.</param>
    /// <param name="sourceChanged">The message when the file holds other bytes.</param>
    /// <returns>The validation.</returns>
    public static SceneEditValidation Validate(SceneEditScope scope, NodeId instance, AssetPath? scene, InstanceChanges? changes, ReadOnlyMemory<byte> expected, ReadOnlyMemory<byte> replacement, string sourceChanged)
    {
        // A deserialized edit can still carry nulls.
        if (scene is null || changes is null || expected.IsEmpty || replacement.IsEmpty)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "Applying to a source names the scene, the instance's changes and the scene file's bytes.", instance));
        }

        if (RefuseModel(scene, instance) is { } model)
        {
            return SceneEditValidation.Invalid(model);
        }

        var node = scope.FindNode(instance)!;
        if (node.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is not { } component)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so it has no changes to apply.", instance));
        }

        if (!changes.MapAssets(asset => scope.Resolve(asset)!).Equals(component.Changes))
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(
                SceneEditCodes.InstanceUnavailable,
                $"The changes of '{node.Metadata.Name}' changed since they were read. Apply them again.",
                instance));
        }

        var resolved = scope.Resolve(scene)!;
        if (scope.SceneFiles is not { } files)
        {
            return SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.InstanceUnavailable, $"{resolved.Name} cannot be written: no project is open.", instance));
        }

        return files.Read(resolved) is { } current && current.AsSpan().SequenceEqual(expected.Span)
            ? SceneEditValidation.Valid
            : SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.SourceChanged, sourceChanged, instance));
    }

    /// <summary>Writes the scene's file and the instance root's changes.</summary>
    /// <param name="scope">The scene and its ports, checked by <see cref="Validate"/>.</param>
    /// <param name="instance">The instance root.</param>
    /// <param name="scene">The authored scene whose file is replaced.</param>
    /// <param name="changes">The changes the instance root holds after the write.</param>
    /// <param name="content">The bytes the scene's file holds after the write.</param>
    /// <param name="failure">Why the file could not be written, or <see langword="null"/> when it was.</param>
    /// <returns>
    /// What changed in this scene, the instance root's changes, or <see langword="null"/> when the file could not be
    /// written, which leaves both scenes as they were.
    /// </returns>
    public static IReadOnlyList<SceneChange>? Write(SceneEditScope scope, NodeId instance, AssetPath scene, InstanceChanges changes, ReadOnlyMemory<byte> content, out SceneDiagnostic? failure)
    {
        var component = scope.FindNode(instance)!.Components.OfType<SceneInstanceComponent>().First();
        var before = component.Changes;
        var resolved = scope.Resolve(scene)!;
        try
        {
            scope.SceneFiles!.Write(resolved, content.ToArray());
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            failure = SceneDiagnostic.Error(SceneEditCodes.InstanceUnavailable, $"{resolved.Name} could not be written: {exception.Message}", instance);
            return null;
        }

        failure = null;
        component.Changes = changes.MapAssets(asset => scope.Resolve(asset)!);
        PropertyId[] properties = [.. Moved(instance, before, component.Changes)
            .SelectMany(reference => before.Of(reference.Path).Components.Values.Concat(component.Changes.Of(reference.Path).Components.Values))
            .SelectMany(static overrides => overrides.All)
            .Select(static change => change.Property)
            .Distinct()];
        return [new SceneChange.OverridesChanged(instance, properties)];
    }
}
