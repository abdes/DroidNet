// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Scripting;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

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
    private readonly Ports ports;

    private AuthoringFixture(TestProject project, DocumentId document, SceneContent content, object historyRoot, HistoryKeeper history, FakeNodeInteraction? interaction, FakeAssetRedirects redirects, FakeAuthoredReferences references, Ports ports, SingleThreadContext? thread)
    {
        this.Project = project;
        this.ports = ports;
        this.Structures = ports.Structures;
        this.Document = document;
        this.Content = content;
        this.historyRoot = historyRoot;
        this.History = history;
        this.Interaction = interaction;
        this.Redirects = redirects;
        this.References = references;
        this.Thread = thread;
        this.Changes = new ChangeRecorder(content.Authoring);
    }

    /// <summary>
    /// Gets what the scene's instances contain, as the editor last read it: a source published here is read, for the
    /// edits' part owners and the interaction's locks alike.
    /// </summary>
    public FakeSceneStructures Structures { get; }

    /// <summary>Gets the project the scene belongs to.</summary>
    public TestProject Project { get; }

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

    /// <summary>Gets the saved authored references the scene's instance sources are checked against.</summary>
    public FakeAuthoredReferences References { get; }

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
        var references = new FakeAuthoredReferences();
        var root = new object();
        var history = new HistoryKeeper(root);
        var project = TestProject.Create(new MockFileSystem());
        var document = DocumentId.New();
        var ports = Ports.For(project, interaction);
        try
        {
            var content = await DecodeAsync(project, data, document, interaction, redirects, references, ports, history).ConfigureAwait(true);
            return new AuthoringFixture(project, document, content, root, history, interaction, redirects, references, ports, thread: null);
        }
        catch
        {
            ports.Dispose();
            throw;
        }
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
        Ports? ports = null;
        try
        {
            var interaction = new FakeNodeInteraction();
            var redirects = new FakeAssetRedirects();
            var references = new FakeAuthoredReferences();
            var root = new object();
            var history = new HistoryKeeper(root);
            var project = TestProject.Create(new MockFileSystem());
            var document = DocumentId.New();
            var created = Ports.For(project, interaction);
            ports = created;
            var content = await thread.RunAsync(() => DecodeAsync(project, data, document, interaction, redirects, references, created, history)).ConfigureAwait(false);
            return new AuthoringFixture(project, document, content, root, history, interaction, redirects, references, created, thread);
        }
        catch
        {
            ports?.Dispose();
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
    /// Reads the script slots of a node of the live scene.
    /// </summary>
    /// <param name="node">The node's id.</param>
    /// <returns>The slots, in order.</returns>
    /// <exception cref="InvalidOperationException">The scene has no such node.</exception>
    public IReadOnlyList<ScriptSlot> SlotsOf(NodeId node)
        => [.. this.Node(node).Components.OfType<ScriptingComponent>().Single().Slots];

    /// <summary>
    /// Finds a script slot of a node of the live scene.
    /// </summary>
    /// <param name="node">The node's id.</param>
    /// <param name="slot">The slot's id.</param>
    /// <returns>The slot.</returns>
    /// <exception cref="InvalidOperationException">The scene has no such node.</exception>
    public ScriptSlot SlotOf(NodeId node, ScriptSlotId slot)
        => this.SlotsOf(node).Single(item => item.Id == slot.Value);

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

        this.ports.Dispose();
        GC.KeepAlive(this.historyRoot);
    }

    private static async Task<SceneContent> DecodeAsync(TestProject project, SceneData data, DocumentId document, FakeNodeInteraction? interaction, FakeAssetRedirects redirects, FakeAuthoredReferences references, Ports ports, HistoryKeeper history)
    {
        var kind = new SceneDocumentKind(interaction, redirects, references, ports.Session, ports.Structures);
        var file = new DocumentFile(
            project.PathOf($"/Content/{data.Name}{AssetFileTypes.SceneDescriptor}"),
            AssetPath.Parse($"/Content/{data.Name}{AssetFileTypes.Scene}"));
        var bytes = JsonSerializer.SerializeToUtf8Bytes(data as SceneFileData ?? new SceneFileData(data, explorerLayout: null), SceneFileJsonContext.Default.SceneFileData);
        var result = await kind.DecodeAsync(file, bytes, new DocumentDecodeContext(document, history), CancellationToken.None).ConfigureAwait(true);
        return result.Content as SceneContent ?? throw new InvalidOperationException($"The test scene did not decode: {result.Problem}");
    }

    // The session and the structures the scene's edits and the interaction's locks read alike; it owns the session.
    private sealed class Ports : IDisposable
    {
        private Ports(TestProject project)
        {
            this.Session = new TestSession(project.Context);
        }

        public TestSession Session { get; }

        public FakeSceneStructures Structures { get; } = new();

        public static Ports For(TestProject project, FakeNodeInteraction? interaction)
        {
            var ports = new Ports(project);
            interaction?.StructureOf = source => ports.Structures.Find(project.Context, source);
            return ports;
        }

        public void Dispose() => this.Session.Dispose();
    }
}
