// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Shows relocated content again in the engine once it is mounted, and resolves the references that undo, redo and
/// paste captured before a relocation.
/// </summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The scene already follows the relocation; a runtime refresh failure is logged and the runtime keeps its previous view.")]
    public async Task RefreshRelocatedRuntimeAsync(SceneDocumentCommandContext context, IReadOnlyCollection<NodeId> nodeIds, bool environment)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);
        if (!ReferenceEquals(this.sceneEngineSync.GetDocumentScene(context.DocumentId), context.Scene))
        {
            return;
        }

        try
        {
            var nodes = nodeIds.Select(id => FindNode(context.Scene, id)).OfType<SceneNode>().ToArray();
            foreach (var node in nodes)
            {
                _ = await this.sceneEngineSync.AttachGeometryAsync(context.Scene, node).ConfigureAwait(true);
            }

            if (environment)
            {
                _ = await this.sceneEngineSync.UpdateEnvironmentAsync(context.Scene, context.Scene.Environment).ConfigureAwait(true);
            }

            this.LogRuntimeRefreshedAfterRelocation(context.Scene.Name, nodes.Length, environment);
        }
        catch (Exception failure)
        {
            this.LogRuntimeRefreshFailed(failure, context.Scene.Name);
        }
    }

    private static IEnumerable<Uri?> ReferencedUris(SceneNode node)
        => SceneReferenceRetargeting.RuntimeReferences(node).Concat(SceneReferenceRetargeting.ScriptReferences(node));

    /// <summary>Resolves a reference captured before a relocation to the asset's current identity.</summary>
    /// <param name="uri">The captured reference.</param>
    /// <returns>The current reference, or the input when the asset did not move.</returns>
    private Uri? RedirectCaptured(Uri? uri) => uri is null || this.redirects is null ? uri : this.redirects.Resolve(uri);

    // Same, in the shape the retargeting helpers take: null when the reference does not move.
    private Uri? RedirectOrNull(Uri uri)
    {
        var resolved = this.RedirectCaptured(uri);
        return resolved is null || resolved == uri ? null : resolved;
    }

    private string? RedirectOrNull(string reference)
    {
        if (this.redirects is null)
        {
            return null;
        }

        var resolved = this.redirects.Resolve(reference);
        return string.Equals(resolved, reference, StringComparison.Ordinal) ? null : resolved;
    }

    /// <summary>Re-points a restored or pasted subtree's captured references to their current identities.</summary>
    /// <param name="context">The document, for the deleted-asset warning.</param>
    /// <param name="node">The restored or pasted subtree.</param>
    private void RedirectCapturedSubtree(SceneDocumentCommandContext context, SceneNode node)
    {
        if (this.redirects is null)
        {
            return;
        }

        _ = SceneReferenceRetargeting.RetargetSubtree(node, this.RedirectOrNull);
        this.WarnIfDeleted(context, new[] { node }.Concat(node.Descendants()).SelectMany(ReferencedUris));
    }

    private SceneReferencesData RedirectCaptured(SceneReferencesData references)
        => SceneReferenceRetargeting.Retarget(references, this.RedirectOrNull) ?? references;

    private SceneEnvironmentData RedirectCaptured(SceneDocumentCommandContext context, SceneEnvironmentData environment)
    {
        var resolved = SceneReferenceRetargeting.Retarget(environment, this.RedirectOrNull) ?? environment;
        this.WarnIfDeleted(context, SceneReferenceRetargeting.ReferencedUris(resolved));
        return resolved;
    }

    /// <summary>Restoring a reference to a deleted asset completes as a missing reference, with a visible warning.</summary>
    private void WarnIfDeleted(SceneDocumentCommandContext context, IEnumerable<Uri?> restored)
    {
        if (this.redirects is null)
        {
            return;
        }

        var deleted = restored.OfType<Uri>().Where(this.redirects.WasDeleted).Distinct().ToArray();
        if (deleted.Length == 0)
        {
            return;
        }

        var names = string.Join(", ", deleted.Select(static uri => Path.GetFileName(Uri.UnescapeDataString(uri.AbsolutePath))));
        this.LogRestoredDeletedReference(context.Scene.Name, names);
        _ = this.PublishSceneWarning(
            SceneOperationKinds.Mutation,
            DiagnosticCodes.ScenePrefix + "RESTORED_DELETED_ASSET",
            "Restored a reference to a deleted asset",
            $"{names} was deleted, so the restored reference shows as missing until you choose another asset.",
            context);
    }
}
