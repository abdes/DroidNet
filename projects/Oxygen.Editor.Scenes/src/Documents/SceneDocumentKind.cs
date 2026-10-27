// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>The scene document kind: opens <c>.oscene.json</c> files as scenes of a project session.</summary>
/// <param name="interaction">The nodes hidden and locked in the editor, if the editor keeps them.</param>
/// <param name="redirects">The session's asset renames and moves, if the editor tracks them.</param>
/// <param name="references">The saved authored references instance sources are checked against, if the editor reads them.</param>
/// <param name="session">The project session the scenes belong to, whose instances' structures are read and whose scene files <i>Apply to source</i> writes.</param>
/// <param name="structures">What the instances of the shown scene contain, if the editor reads it.</param>
public sealed class SceneDocumentKind(
    ISceneNodeInteraction? interaction = null,
    IAssetRedirects? redirects = null,
    IAuthoredReferences? references = null,
    IProjectSession? session = null,
    ISceneStructures? structures = null) : IDocumentKind
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
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "The decoded SceneContent is handed to the DocumentDecodeResult, whose consumer (the opened document) owns and disposes it; nothing between its construction and the return can throw.")]
    public async Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(file);
        ArgumentNullException.ThrowIfNull(context);

        SceneFileData data;
        Scene scene;
        try
        {
            var stream = new MemoryStream(content.ToArray(), writable: false);
            await using var lifetime = stream.ConfigureAwait(true);
            data = await JsonSerializer.DeserializeAsync(stream, SceneFileJsonContext.Default.SceneFileData, cancellationToken).ConfigureAwait(true)
                ?? throw new JsonException("The file holds no scene.");
            cancellationToken.ThrowIfCancellationRequested();
            scene = Scene.CreateAndHydrate(data);
        }
        catch (Exception exception) when (exception is JsonException or ArgumentException)
        {
            // Malformed JSON, a node without its required components or a value the scene model rejects: the file is
            // not a scene this editor opens, and the document fails to open with the reason.
            return DocumentDecodeResult.Failed($"'{file.Path.Name}' is not a valid scene: {exception.Message}");
        }

        var layout = data.ExplorerLayout ?? [];
        var nodeIds = new HashSet<NodeId>();
        if (scene.AllNodes.Any(node => node.Id.IsEmpty || !nodeIds.Add(node.Id)))
        {
            return DocumentDecodeResult.Failed($"'{file.Path.Name}' has nodes with missing or duplicate identities.");
        }

        if (!IsLayoutValid(layout, nodeIds, [], []))
        {
            return DocumentDecodeResult.Failed($"'{file.Path.Name}' has an explorer layout with duplicate or foreign identities.");
        }

        scene.Name = SceneContent.NameOf(file.Path.Name);

        // Every node gets an explorer entry now, so edits always find one and their reverts restore it exactly.
        SceneLayout.Complete(scene, layout);
        Func<AssetPath, SceneStructure?>? structureOf = session is not null && structures is not null
            ? source => structures.Find(session.Context, source)
            : null;
        return DocumentDecodeResult.Decoded(new SceneContent(scene, layout, context, interaction, redirects, references, file.Asset, structureOf, session is null ? null : new ProjectSceneFiles(session)));
    }

    private static bool IsLayoutValid(IEnumerable<ExplorerEntryData>? entries, HashSet<NodeId> nodeIds, HashSet<FolderId> folderIds, HashSet<NodeId> layoutNodeIds)
        => entries?.All(entry =>
            (entry.FolderId is not { } folder || folderIds.Add(folder))
            && (entry.NodeId is not { } node || (nodeIds.Contains(node) && layoutNodeIds.Add(node)))
            && IsLayoutValid(entry.Children, nodeIds, folderIds, layoutNodeIds)) != false;
}
