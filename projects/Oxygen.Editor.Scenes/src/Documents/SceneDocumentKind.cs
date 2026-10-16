// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Documents;

/// <summary>The scene document kind: opens <c>.oscene.json</c> files as scenes of the current project.</summary>
/// <param name="projects">The current project, which a decoded scene belongs to.</param>
/// <param name="commands">The command service, which owns each scene's edits in progress.</param>
public sealed class SceneDocumentKind(IProjectManagerService projects, ISceneDocumentCommandService commands) : IDocumentKind
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
    public async Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(file);
        if (projects.CurrentProject is not { } project)
        {
            return DocumentDecodeResult.Failed("No project is open.");
        }

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
        return DocumentDecodeResult.Decoded(new SceneContent(scene, commands));
    }

    private static bool IsLayoutValid(IEnumerable<ExplorerEntryData>? entries, HashSet<NodeId> nodeIds, HashSet<FolderId> folderIds, HashSet<NodeId> layoutNodeIds)
        => entries is null || entries.All(entry =>
            (entry.FolderId is not { } folder || folderIds.Add(folder))
            && (entry.NodeId is not { } node || (nodeIds.Contains(node) && layoutNodeIds.Add(node)))
            && IsLayoutValid(entry.Children, nodeIds, folderIds, layoutNodeIds));
}
