// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>The scene document kind: opens <c>.oscene.json</c> files as scenes of a project.</summary>
/// <param name="project">The project that every scene this kind decodes belongs to.</param>
/// <param name="interaction">The nodes hidden and locked in the editor, if the editor keeps them.</param>
/// <param name="redirects">The session's asset renames and moves, if the editor tracks them.</param>
public sealed class SceneDocumentKind(
    IProject project,
    ISceneNodeInteraction? interaction = null,
    IAssetRedirects? redirects = null) : IDocumentKind
{
    /// <inheritdoc/>
    public string Name => "Scene";

    /// <inheritdoc/>
    public IReadOnlyList<string> FileSuffixes { get; } = [AssetFileTypes.SceneDescriptor];

    /// <inheritdoc/>
    /// <remarks>
    /// The scene takes its name from its file. Its node identities must be present and unique, and its explorer layout
    /// must name each folder once and only nodes of the scene, each once.
    /// </remarks>
    public async Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(file);
        ArgumentNullException.ThrowIfNull(context);

        Scene scene;
        try
        {
            var stream = new MemoryStream(content.ToArray(), writable: false);
            await using var lifetime = stream.ConfigureAwait(true);
            scene = await new SceneSerializer(project).DeserializeAsync(stream).ConfigureAwait(true);
        }
        catch (JsonException exception)
        {
            return DocumentDecodeResult.Failed($"'{Path.GetFileName(file.Path)}' is not a valid scene: {exception.Message}");
        }

        cancellationToken.ThrowIfCancellationRequested();
        var nodeIds = new HashSet<NodeId>();
        if (scene.AllNodes.Any(node => node.Id.IsEmpty || !nodeIds.Add(node.Id)))
        {
            return DocumentDecodeResult.Failed($"'{Path.GetFileName(file.Path)}' has nodes with missing or duplicate identities.");
        }

        if (!IsLayoutValid(scene.ExplorerLayout, nodeIds, [], []))
        {
            return DocumentDecodeResult.Failed($"'{Path.GetFileName(file.Path)}' has an explorer layout with duplicate or foreign identities.");
        }

        scene.Name = SceneContent.NameOf(file.Path);

        // Every node gets an explorer entry now, so edits always find one and their reverts restore it exactly.
        SceneLayout.Complete(scene);
        return DocumentDecodeResult.Decoded(new SceneContent(scene, context, interaction, redirects));
    }

    private static bool IsLayoutValid(IEnumerable<ExplorerEntryData>? entries, HashSet<NodeId> nodeIds, HashSet<FolderId> folderIds, HashSet<NodeId> layoutNodeIds)
        => entries is null || entries.All(entry =>
            (entry.FolderId is not { } folder || folderIds.Add(folder))
            && (entry.NodeId is not { } node || (nodeIds.Contains(node) && layoutNodeIds.Add(node)))
            && IsLayoutValid(entry.Children, nodeIds, folderIds, layoutNodeIds));
}
