// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Documents;

/// <summary>What a scene document edits: the scene, and the work on it that needs to know the scene format.</summary>
/// <remarks>
/// Disposing the content retires its scene: commands already running finish, and no new command starts on it. A reload
/// replaces the content, and closing the document disposes it.
/// </remarks>
public sealed class SceneContent : IDocumentContent, IDisposable
{
    private readonly ISceneDocumentCommandService commands;

    /// <summary>Initializes a new instance of the <see cref="SceneContent"/> class.</summary>
    /// <param name="scene">The decoded scene.</param>
    /// <param name="commands">The command service, which owns the scene's edits in progress.</param>
    internal SceneContent(Scene scene, ISceneDocumentCommandService commands)
    {
        this.Scene = scene;
        this.commands = commands;
    }

    /// <summary>Occurs when the content applied a relocation, naming what the engine must show again.</summary>
    public event EventHandler<SceneRelocatedEventArgs>? Relocated;

    /// <summary>Gets the scene.</summary>
    public Scene Scene { get; }

    /// <summary>Gets the scene's name for a scene file: its file name without the scene suffix.</summary>
    /// <param name="path">The scene file's path.</param>
    /// <returns>The scene's name.</returns>
    public static string NameOf(string path)
    {
        var name = Path.GetFileName(path);
        return name.EndsWith(AssetFileTypes.SceneDescriptor, StringComparison.OrdinalIgnoreCase)
            ? name[..^AssetFileTypes.SceneDescriptor.Length]
            : Path.GetFileNameWithoutExtension(name);
    }

    /// <inheritdoc/>
    public bool HasEditsInProgress => this.commands.HasEditInProgress(this.Scene);

    /// <inheritdoc/>
    public Task FinishEditsAsync(CancellationToken cancellationToken) => this.commands.CompleteEditSessionsAsync(this.Scene, commit: true);

    /// <inheritdoc/>
    public DocumentCapture Capture()
        => SceneAuthoringGate.IsRetired(this.Scene)
            ? DocumentCapture.Refused("The scene was closed or replaced before it could be saved.")
            : Encoded(this.Scene.Dehydrate());

    /// <inheritdoc/>
    public DocumentCapture CaptureCopy(DocumentFile copy)
    {
        ArgumentNullException.ThrowIfNull(copy);
        return Encoded(this.Scene.Dehydrate() with { Id = SceneId.New(), Name = NameOf(copy.Path) });
    }

    /// <inheritdoc/>
    /// <remarks>
    /// The relocation runs only while the scene is saved, so its file before the rewrite equals this model. The model is
    /// re-pointed to match the rewritten file; nothing enters the history, and the scene takes its file's new name.
    /// </remarks>
    public void ApplyRelocation(DocumentRelocation relocation)
    {
        ArgumentNullException.ThrowIfNull(relocation);
        var scene = this.Scene;
        var nodes = scene.AllNodes.Where(node => SceneReferenceRetargeting.RetargetNode(node, relocation.MapUri)).ToArray();
        if (SceneReferenceRetargeting.Retarget(scene.References, relocation.MapReference) is { } references)
        {
            scene.SetReferences(references);
        }

        if (SceneReferenceRetargeting.Retarget(scene.Play, relocation.MapUri) is { } play)
        {
            scene.SetPlay(play);
        }

        var environment = SceneReferenceRetargeting.Retarget(scene.Environment, relocation.MapUri);
        if (environment is not null)
        {
            scene.SetEnvironment(environment);
        }

        scene.Name = NameOf(relocation.File.Path);

        // Script slots have no runtime projection; only geometry is attached again.
        this.Relocated?.Invoke(this, new SceneRelocatedEventArgs(
            [.. nodes.Where(static node => node.Components.OfType<GeometryComponent>().Any()).Select(static node => node.Id)],
            environment is not null));
    }

    /// <inheritdoc/>
    public void Dispose() => SceneAuthoringGate.Retire(this.Scene);

    private static DocumentCapture Encoded(SceneData data)
    {
        // The data is serialized now: it shares collections with the scene, which goes on changing.
        ReadOnlyMemory<byte> bytes = JsonSerializer.SerializeToUtf8Bytes(data, SceneJsonContext.Default.SceneData);
        return DocumentCapture.Of(_ => Task.FromResult(bytes));
    }
}
