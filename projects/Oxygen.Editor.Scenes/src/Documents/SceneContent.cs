// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>What a scene document edits: the scene, and the work on it that needs to know the scene format.</summary>
/// <remarks>
/// Disposing the content retires its authoring: edits already running finish, and no new edit starts on it. A reload
/// replaces the content, and closing the document disposes it.
/// </remarks>
public sealed class SceneContent : IDocumentContent, IDisposable
{
    /// <summary>Initializes a new instance of the <see cref="SceneContent"/> class.</summary>
    /// <param name="scene">The decoded scene.</param>
    /// <param name="document">The identity and history of the document the scene is decoded for.</param>
    /// <param name="interaction">The nodes hidden and locked in the editor, if the editor keeps them.</param>
    /// <param name="redirects">The session's asset renames and moves, if the editor tracks them.</param>
    internal SceneContent(Scene scene, DocumentDecodeContext document, ISceneNodeInteraction? interaction, IAssetRedirects? redirects)
    {
        this.Scene = scene;
        this.Authoring = new SceneAuthoring(document, scene, interaction, redirects);
    }

    /// <summary>Gets the scene.</summary>
    public Scene Scene { get; }

    /// <summary>Gets the authoring that executes every change to the scene.</summary>
    public SceneAuthoring Authoring { get; }

    /// <summary>Gets the content of a scene document.</summary>
    /// <param name="document">The document.</param>
    /// <returns>The document's scene content.</returns>
    /// <exception cref="ArgumentException">The document is not a scene document.</exception>
    public static SceneContent Of(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return document.Content as SceneContent ?? throw new ArgumentException($"'{document.Title}' is not a scene document.", nameof(document));
    }

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
    public bool HasEditsInProgress => this.Authoring.HasEditsInProgress;

    /// <inheritdoc/>
    public Task FinishEditsAsync(CancellationToken cancellationToken) => this.Authoring.FinishEditsAsync();

    /// <inheritdoc/>
    public DocumentCapture Capture()
        => this.Authoring.IsRetired
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
        var applied = this.Authoring.ChangeOutsideHistoryAsync(scene => Relocate(scene, relocation));
        if (!applied.IsCompleted)
        {
            throw new InvalidOperationException("A scene follows a relocation on its own thread.");
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Authoring.Dispose();
    }

    // Script slots have no runtime projection; the projector attaches the re-pointed geometry once it is mounted.
    private static IReadOnlyList<SceneChange> Relocate(Scene scene, DocumentRelocation relocation)
    {
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
        var changes = new List<SceneChange>(nodes
            .Where(static node => node.Components.OfType<GeometryComponent>().Any())
            .Select(static node => new SceneChange.GeometryChanged(node.Id)));
        if (environment is not null)
        {
            changes.Add(new SceneChange.PropertiesChanged(Node: null, [.. SceneProperties.RulesOf(SceneProperties.SceneEnvironmentKind)!.Descriptors.Keys], EditPhase.Commit));
        }

        return changes;
    }

    private static DocumentCapture Encoded(SceneData data)
    {
        // The data is serialized now: it shares collections with the scene, which goes on changing.
        ReadOnlyMemory<byte> bytes = JsonSerializer.SerializeToUtf8Bytes(data, SceneJsonContext.Default.SceneData);
        return DocumentCapture.Of(_ => Task.FromResult(bytes));
    }
}
