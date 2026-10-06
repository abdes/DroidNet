// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Owns scene-level authored asset-reference edits.</summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc />
    public async Task<SceneCommandResult> EditSceneReferencesAsync(
        SceneDocumentCommandContext context,
        SceneReferencesData references)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new(Succeeded: false);
        }

        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(references);

        if (ValidateReferences(references) is { } error)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditSceneReferences,
                "SCENE_REFERENCES_INVALID",
                "Scene references were not changed",
                error,
                context);
        }

        var before = CopyReferences(context.Scene.References);
        var after = CopyReferences(references);
        if (ReferencesEqual(before, after))
        {
            return SceneCommandResult.Success;
        }

        context.Scene.SetReferences(after);
        context.History.AddChange("Restore Scene References", async () =>
            await this.ApplySceneReferencesForHistoryAsync(context, before, after).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        return SceneCommandResult.Success;
    }

    private async Task ApplySceneReferencesForHistoryAsync(
        SceneDocumentCommandContext context,
        SceneReferencesData references,
        SceneReferencesData inverse)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        context.Scene.SetReferences(CopyReferences(references));
        context.History.AddChange("Reapply Scene References", async () =>
            await this.ApplySceneReferencesForHistoryAsync(context, inverse, references).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
    }

    private static string? ValidateReferences(SceneReferencesData references)
    {
        if (references.Scripts is null || references.InputActions is null
            || references.InputMappingContexts is null || references.PhysicsSidecars is null
            || references.ExtraAssets is null)
        {
            return "Scene reference collections cannot be null.";
        }

        if (ValidateTypedReferences(references.Scripts, ".oscript") is { } scripts)
        {
            return scripts;
        }

        if (ValidateTypedReferences(references.InputActions, ".oiact") is { } actions)
        {
            return actions;
        }

        if (ValidateTypedReferences(references.InputMappingContexts, ".oimap") is { } mappings)
        {
            return mappings;
        }

        if (ValidateTypedReferences(references.PhysicsSidecars, ".opscene") is { } physics)
        {
            return physics;
        }

        foreach (var path in references.ExtraAssets)
        {
            if (string.IsNullOrWhiteSpace(path) || !ContentPipelinePaths.IsCanonicalVirtualPath(path))
            {
                return $"Extra asset path '{path}' must be an absolute canonical virtual path.";
            }
        }

        return null;
    }

    private static string? ValidateTypedReferences(IEnumerable<Uri> references, string expectedExtension)
    {
        foreach (var uri in references)
        {
            if (uri is null || !uri.IsAbsoluteUri
                || !string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
            {
                return $"Scene references must use absolute '{AssetUris.Scheme}' asset URIs.";
            }

            try
            {
                _ = ContentPipelinePaths.ToNativeDescriptorPath(uri, expectedExtension);
            }
            catch (ArgumentException error)
            {
                return error.Message;
            }
        }

        return null;
    }

    private static SceneReferencesData CopyReferences(SceneReferencesData references)
        => new()
        {
            Scripts = [.. references.Scripts],
            InputActions = [.. references.InputActions],
            InputMappingContexts = [.. references.InputMappingContexts],
            PhysicsSidecars = [.. references.PhysicsSidecars],
            ExtraAssets = [.. references.ExtraAssets],
        };

    private static bool ReferencesEqual(SceneReferencesData left, SceneReferencesData right)
        => left.Scripts.SequenceEqual(right.Scripts)
            && left.InputActions.SequenceEqual(right.InputActions)
            && left.InputMappingContexts.SequenceEqual(right.InputMappingContexts)
            && left.PhysicsSidecars.SequenceEqual(right.PhysicsSidecars)
            && left.ExtraAssets.SequenceEqual(right.ExtraAssets, StringComparer.Ordinal);
}
