// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// One open scene document: the scene data serialized and decoded by the real <see cref="SceneDocumentKind"/>, with a
/// real history, the hand-rolled fakes for the editor's ports, and a recorder of its changes.
/// </summary>
/// <remarks>
/// <see cref="CreateAsync"/> decodes on the caller's context; under MSTest there is none, so every authoring call
/// completes synchronously on the calling thread. <see cref="CreateOnThreadAsync"/> decodes on a
/// <see cref="SingleThreadContext"/>, which then owns the scene, as a UI thread does in the editor.
/// </remarks>
public sealed partial class AuthoringFixture : IDisposable
{
    private readonly object historyRoot;

    private AuthoringFixture(DocumentId document, SceneContent content, object historyRoot, HistoryKeeper history, FakeNodeInteraction? interaction, FakeAssetRedirects redirects, SingleThreadContext? thread)
    {
        this.Document = document;
        this.Content = content;
        this.historyRoot = historyRoot;
        this.History = history;
        this.Interaction = interaction;
        this.Redirects = redirects;
        this.Thread = thread;
        this.Changes = new ChangeRecorder(content.Authoring);
    }

    /// <summary>Gets the decoded document content.</summary>
    public SceneContent Content { get; }

    /// <summary>Gets the id the document was decoded with.</summary>
    public DocumentId Document { get; }

    /// <summary>Gets the scene's authoring frame.</summary>
    public SceneAuthoring Authoring => this.Content.Authoring;

    /// <summary>Gets the live scene.</summary>
    public Scene Scene => this.Content.Scene;

    /// <summary>Gets the live scene's explorer layout.</summary>
    public IList<ExplorerEntryData> Layout => this.Authoring.Layout;

    /// <summary>Gets the document's history.</summary>
    public HistoryKeeper History { get; }

    /// <summary>Gets the recorder of the scene's <c>Changed</c> events.</summary>
    public ChangeRecorder Changes { get; }

    /// <summary>Gets the lock and hide state, or <see langword="null"/> when the scene was opened without it.</summary>
    public FakeNodeInteraction? Interaction { get; }

    /// <summary>Gets the asset moves and deletions the scene's edits see.</summary>
    public FakeAssetRedirects Redirects { get; }

    /// <summary>Gets the thread that owns the scene, when it was opened on one.</summary>
    public SingleThreadContext? Thread { get; }

    /// <summary>
    /// Opens a scene on the caller's context.
    /// </summary>
    /// <param name="data">The scene data, typically <see cref="SampleScene.Build"/>.</param>
    /// <param name="withInteraction">Whether the scene gets the editor's lock and hide port.</param>
    /// <returns>The fixture.</returns>
    public static async Task<AuthoringFixture> CreateAsync(SceneData data, bool withInteraction = true)
    {
        ArgumentNullException.ThrowIfNull(data);
        var interaction = withInteraction ? new FakeNodeInteraction() : null;
        var redirects = new FakeAssetRedirects();
        var root = new object();
        var history = new HistoryKeeper(root);
        var document = DocumentId.New();
        var content = await DecodeAsync(data, document, interaction, redirects, history).ConfigureAwait(true);
        return new AuthoringFixture(document, content, root, history, interaction, redirects, thread: null);
    }

    /// <summary>
    /// Opens a scene on a new <see cref="SingleThreadContext"/>, which the fixture disposes.
    /// </summary>
    /// <param name="data">The scene data.</param>
    /// <returns>The fixture.</returns>
    public static async Task<AuthoringFixture> CreateOnThreadAsync(SceneData data)
    {
        ArgumentNullException.ThrowIfNull(data);
        var thread = new SingleThreadContext();
        try
        {
            var interaction = new FakeNodeInteraction();
            var redirects = new FakeAssetRedirects();
            var root = new object();
            var history = new HistoryKeeper(root);
            var document = DocumentId.New();
            var content = await thread.RunAsync(() => DecodeAsync(data, document, interaction, redirects, history)).ConfigureAwait(false);
            return new AuthoringFixture(document, content, root, history, interaction, redirects, thread);
        }
        catch
        {
            thread.Dispose();
            throw;
        }
    }

    /// <summary>
    /// Gets a node of the live scene.
    /// </summary>
    /// <param name="id">The node's id.</param>
    /// <returns>The node.</returns>
    /// <exception cref="InvalidOperationException">The scene has no such node.</exception>
    public SceneNode Node(NodeId id)
        => this.Scene.AllNodes.FirstOrDefault(node => node.Id == id) ?? throw new InvalidOperationException($"The scene has no node {id}.");

    /// <summary>
    /// Captures what "unchanged" compares: the dehydrated document, the hidden nodes, the history depth and the number
    /// of <c>Changed</c> events so far.
    /// </summary>
    /// <returns>The snapshot.</returns>
    public SceneSnapshot Snapshot()
        => new(
            JsonSerializer.Serialize(new SceneFileData(this.Scene.Dehydrate(), this.Layout), SceneFileJsonContext.Default.SceneFileData),
            string.Join(',', this.Interaction?.Hidden ?? []),
            this.History.UndoStack.Count,
            this.History.RedoStack.Count,
            this.Changes.Count);

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Changes.Dispose();
        if (this.Thread is { } thread)
        {
            // The scene belongs to its thread, as it does in the editor.
            thread.Send(_ => this.Content.Dispose(), state: null);
            thread.Dispose();
        }
        else
        {
            this.Content.Dispose();
        }

        GC.KeepAlive(this.historyRoot);
    }

    private static async Task<SceneContent> DecodeAsync(SceneData data, DocumentId document, FakeNodeInteraction? interaction, FakeAssetRedirects redirects, HistoryKeeper history)
    {
        var kind = new SceneDocumentKind(FakeMaterialSlotProvider.Project, interaction, redirects);
        var file = new DocumentFile(
            Path.Combine(Path.GetTempPath(), "Content", data.Name + AssetFileTypes.SceneDescriptor),
            new Uri($"asset:/Content/{data.Name}{AssetFileTypes.SceneDescriptor}"));
        var bytes = JsonSerializer.SerializeToUtf8Bytes(data as SceneFileData ?? new SceneFileData(data, explorerLayout: null), SceneFileJsonContext.Default.SceneFileData);
        var result = await kind.DecodeAsync(file, bytes, new DocumentDecodeContext(document, history), CancellationToken.None).ConfigureAwait(true);
        return result.Content as SceneContent ?? throw new InvalidOperationException($"The test scene did not decode: {result.Problem}");
    }
}
