// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Documents;
using DroidNet.Storage;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Models;
using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>
/// The scene each open document shows: a scene document's own scene, and a model document's preview scene, which holds
/// one instance of the model at the origin, lit by a physical sky and its sun at a fixed exposure (model editor 4, 5.2).
/// </summary>
/// <remarks>
/// A preview scene is built when first asked for. The model in it is read-only: every edit of the model or of the
/// scene's structure is refused with <see cref="SceneEditCodes.SceneReadOnly"/>, while its environment and its sun take
/// edits, recorded in the preview's own history so the model's document never shows a change. Those edits are kept in
/// the preview file beside the model (<see cref="AssetFileTypes.ModelPreview"/>), written shortly after each change and
/// when the document closes, so the model reopens as it was left; the scene's identity is the model's source identity,
/// which keeps per-user view state, such as the viewport camera, across renames. A preview lives as long as its
/// document's content: a reload builds it again, and closing the document disposes it. Call the service on the
/// documents' thread.
/// </remarks>
public sealed partial class DocumentScenes : IDisposable
{
    // The preview's sun: the illuminance of the physical sun, which the sky atmosphere takes as its light.
    private const float SunIlluminanceLux = 100_000f;

    // The preview's fixed exposure: the EV100 that shows a sunlit model as a camera would, whatever the sky behind it.
    private const float PreviewExposureEv = 14f;

    // How long a preview's changes settle before its file is written: a slider drag writes it once.
    private static readonly TimeSpan SaveDelay = TimeSpan.FromMilliseconds(500);

    private readonly OpenDocuments documents;
    private readonly ISceneNodeInteraction? interaction;
    private readonly IAssetRedirects? redirects;
    private readonly IProjectSession? session;
    private readonly ISceneStructures? structures;
    private readonly ModelPreviewFiles? previewFiles;
    private readonly ILogger logger;
    private readonly Dictionary<DocumentId, Preview> previews = [];

    // The preview files' writes, one at a time, in the order of the changes.
    private Task writing = Task.CompletedTask;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="DocumentScenes"/> class.</summary>
    /// <param name="documents">The session's open documents.</param>
    /// <param name="interaction">The nodes hidden and locked in the editor, if the editor keeps them.</param>
    /// <param name="redirects">The session's asset renames and moves, if the editor tracks them.</param>
    /// <param name="session">The project session, if a project is open.</param>
    /// <param name="structures">What an instance of a source contains, if the editor reads it.</param>
    /// <param name="files">Writes the model previews' files, which a project session keeps with <paramref name="session"/>.</param>
    /// <param name="loggerFactory">Creates the logger that reports the preview files that cannot be read, shown or written.</param>
    public DocumentScenes(
        OpenDocuments documents,
        ISceneNodeInteraction? interaction = null,
        IAssetRedirects? redirects = null,
        IProjectSession? session = null,
        ISceneStructures? structures = null,
        IAtomicFileStore? files = null,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(documents);
        this.documents = documents;
        this.interaction = interaction;
        this.redirects = redirects;
        this.session = session;
        this.structures = structures;
        this.logger = loggerFactory?.CreateLogger<DocumentScenes>() ?? NullLogger<DocumentScenes>.Instance;
        this.previewFiles = session is not null && files is not null ? new ModelPreviewFiles(session, files, this.logger) : null;
        this.documents.Closed += this.OnDocumentClosed;
    }

    /// <summary>Gets a value indicating whether a document shows a model's preview scene.</summary>
    /// <param name="document">The document.</param>
    /// <returns><see langword="true"/> for a model document.</returns>
    public static bool ShowsModel(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return document.Content is ModelContent;
    }

    /// <summary>Gets the scene a document shows.</summary>
    /// <param name="document">The document.</param>
    /// <returns>
    /// The scene document's content, the model document's preview scene, or <see langword="null"/> for a document that
    /// shows no scene.
    /// </returns>
    public SceneContent? Of(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return document.Content switch
        {
            SceneContent scene => scene,
            ModelContent model when !this.disposed => this.PreviewOf(document, model),
            _ => null,
        };
    }

    /// <summary>Gets how a model document's preview names one of the model's nodes: that node below the preview's instance
    /// of the model, as a click in the viewport or the Scene Explorer selects it.</summary>
    /// <param name="document">The model document.</param>
    /// <param name="node">The node's ID in the model, as its import publishes it.</param>
    /// <returns>The reference, or <see langword="null"/> for a document that shows no model.</returns>
    public NodeReference? ReferenceOf(Document document, NodeId node)
    {
        ArgumentNullException.ThrowIfNull(document);
        return ShowsModel(document) && this.Of(document)?.Scene.RootNodes.FirstOrDefault() is { } instance
            ? new NodeReference(instance.Id, NodePath.Of(node))
            : null;
    }

    /// <summary>Finds the open document that shows a scene.</summary>
    /// <param name="scene">The scene.</param>
    /// <returns>The document, or <see langword="null"/> when no open document shows the scene.</returns>
    public Document? DocumentOf(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        return this.documents.Documents.FirstOrDefault(document => ReferenceEquals(this.Of(document)?.Scene, scene));
    }

    /// <summary>
    /// Shows or hides a model preview's sky and sun and sets the stops added to its exposure, keeping every other setting
    /// of its environment. It changes the preview outside its history, as a view setting, and the engine shows the
    /// change.
    /// </summary>
    /// <param name="document">The model document.</param>
    /// <param name="environment">How the preview is lit and exposed.</param>
    /// <returns>A task that completes when the preview took the environment; at once for a document that shows no model.</returns>
    public Task SetPreviewEnvironmentAsync(Document document, PreviewEnvironment environment)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(environment);
        if (this.disposed || document.Content is not ModelContent model)
        {
            return Task.CompletedTask;
        }

        var preview = this.PreviewOf(document, model);
        return ReadEnvironment(preview.Scene) == environment
            ? Task.CompletedTask
            : preview.Authoring.ChangeOutsideHistoryAsync(scene => ApplyEnvironment(scene, environment));
    }

    /// <summary>Gets how a model preview is lit and exposed.</summary>
    /// <param name="document">The model document.</param>
    /// <returns>The preview's environment, as edited; the default for a document that shows no model or no preview yet.</returns>
    public PreviewEnvironment EnvironmentOf(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return this.previews.TryGetValue(document.Id, out var preview) && ReferenceEquals(preview.Model, document.Content)
            ? ReadEnvironment(preview.Content.Scene)
            : PreviewEnvironment.Default;
    }

    /// <summary>
    /// Returns a model's preview to the default: its preview file is deleted and the document reloaded, which builds the
    /// preview again.
    /// </summary>
    /// <param name="document">The model document.</param>
    /// <param name="cancellationToken">Cancels the reload.</param>
    /// <returns><see langword="true"/> when the model shows the default preview.</returns>
    public async Task<bool> ResetPreviewAsync(Document document, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(document);
        if (this.disposed || document.Content is not ModelContent)
        {
            return false;
        }

        if (this.previews.TryGetValue(document.Id, out var preview))
        {
            preview.DiscardUnsaved();
        }

        // A write already started would put the file back.
        await this.writing.ConfigureAwait(true);
        if (this.previewFiles is { } files && !files.Delete(ModelPreviewFiles.PathOf(document)))
        {
            return false;
        }

        return (await document.ReloadAsync(cancellationToken).ConfigureAwait(true)).Succeeded;
    }

    /// <summary>Writes every model preview's changes now, rather than once they settle, and waits for the writes.</summary>
    /// <returns>A task that completes when every preview file holds its preview's last change.</returns>
    public Task FlushAsync()
    {
        foreach (var (id, preview) in this.previews)
        {
            this.WriteUnsaved(this.documents.Find(id), preview);
        }

        return this.writing;
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.documents.Closed -= this.OnDocumentClosed;
        foreach (var (id, preview) in this.previews)
        {
            this.Release(this.documents.Find(id), preview);
        }

        this.previews.Clear();
    }

    // The preview is the model as any scene places it: the instancing does all the work. Its last root is the sun. The
    // environment and the sun are the kept ones, or the defaults. The scene is named apart from the model it places:
    // "Car.glb preview" holds "Car.glb".
    private static Scene BuildPreviewScene(string name, ModelContent model, SceneId id, SceneData? kept)
    {
        var previewName = $"{name} preview";
        var draft = new Scene { Name = previewName, Id = id };
        return Scene.CreateAndHydrate(new SceneData
        {
            Id = id,
            Name = previewName,
            RootNodes =
            [
                SceneNodeTemplates.Instance(draft, model.Source, name: name),
                kept?.RootNodes.FirstOrDefault(static node => node.Components.OfType<DirectionalLightData>().Any()) ?? DefaultSun(draft),
            ],
            Environment = kept?.Environment ?? DefaultEnvironment(),
        });
    }

    // The physical sun, which the sky atmosphere takes as its light.
    private static SceneNodeData DefaultSun(Scene draft)
    {
        var sun = SceneNodeTemplates.Light(draft, LightKind.Directional);
        sun.Components = [.. sun.Components.Select(static component => component is DirectionalLightData light
            ? light with { AtmosphereSlot = AtmosphereLightSlot.Primary, IntensityLux = SunIlluminanceLux }
            : component)];
        return sun;
    }

    // A physical sky lit by the sun, a sky light captured from it, and a fixed exposure: the background never drives
    // what the model looks like.
    private static SceneEnvironmentData DefaultEnvironment()
    {
        var environment = new SceneEnvironmentData();
        environment.PostProcess = environment.PostProcess with
        {
            ExposureMode = ExposureMode.Manual,
            ManualExposureEv = PreviewExposureEv,
        };
        return environment;
    }

    private static DirectionalLightComponent SunOf(Scene scene) => scene.RootNodes[^1].Components.OfType<DirectionalLightComponent>().Single();

    private static PreviewEnvironment ReadEnvironment(Scene scene)
        => new(
            scene.Environment.AtmosphereEnabled && SunOf(scene).AffectsWorld,
            scene.Environment.PostProcess.ExposureCompensationEv);

    private static List<SceneChange> ApplyEnvironment(Scene scene, PreviewEnvironment environment)
    {
        var current = scene.Environment;
        scene.SetEnvironment(current with
        {
            AtmosphereEnabled = environment.IsShown,
            SkyLight = current.SkyLight with { Enabled = environment.IsShown },
            PostProcess = current.PostProcess with { ExposureCompensationEv = environment.ExposureCompensationEv },
        });
        SunOf(scene).AffectsWorld = environment.IsShown;
        return
        [
            new SceneChange.PropertiesChanged(Node: null, [.. SceneProperties.RulesOf(SceneProperties.SceneEnvironmentKind)!.Descriptors.Keys], EditPhase.Commit),
            new SceneChange.PropertiesChanged(scene.RootNodes[^1].Id, [SceneSchema.DirectionalLight.AffectsWorld.Id], EditPhase.Commit),
        ];
    }

    private SceneContent PreviewOf(Document document, ModelContent model)
    {
        if (this.previews.TryGetValue(document.Id, out var preview))
        {
            if (ReferenceEquals(preview.Model, model))
            {
                return preview.Content;
            }

            // A reload replaced the model's content: its preview goes with it, after keeping its last changes.
            this.Release(document, preview);
        }

        var scene = this.BuildKeptPreviewScene(document, model);
        var layout = new List<ExplorerEntryData>();
        SceneLayout.Complete(scene, layout);

        // The explorer lists the model only: the sun lights the preview and is no part of the model. The sun takes edits
        // of its properties only, which never look for its entry.
        var sun = scene.RootNodes[^1].Id;
        _ = layout.RemoveAll(entry => entry.NodeId == sun);
        Func<AssetPath, SceneStructure?>? structureOf = this.session is { } project && this.structures is { } known
            ? source => known.Find(project.Context, source)
            : null;
        var content = new SceneContent(
            scene,
            layout,
            new DocumentDecodeContext(document.Id, new HistoryKeeper(scene)),
            this.interaction,
            this.redirects,
            structureOf: structureOf,
            readOnly: new SceneReadOnly($"{document.Title} is a model: change one placement in a scene that places it, or every placement in its import settings.")
            {
                EditableNodes = [sun],
            });
        var created = new Preview(model, content);
        created.Changed += (_, _) => this.SaveLater(document, created);
        this.previews[document.Id] = created;
        return content;
    }

    // The kept preview, or the default one when the model has none or it cannot be shown.
    private Scene BuildKeptPreviewScene(Document document, ModelContent model)
    {
        var kept = this.previewFiles?.Read(ModelPreviewFiles.PathOf(document));
        var id = this.SourceIdentityOf(document) ?? kept?.Id ?? SceneId.New();
        if (kept is not null)
        {
            try
            {
                return BuildPreviewScene(document.Title, model, id, kept);
            }
            catch (Exception ex) when (ex is JsonException or ArgumentException or InvalidOperationException)
            {
                this.LogKeptPreviewRejected(document.Title, ex);
            }
        }

        return BuildPreviewScene(document.Title, model, id, kept: null);
    }

    // The identity of the model's source, which its import record keeps across renames.
    private SceneId? SourceIdentityOf(Document document)
    {
        if (this.session?.Context is not { } project)
        {
            return null;
        }

        var records = new ModelImportRecords(project.FileSystem, project.ProjectRoot.FullPath);
        return records.TryFindBySource(document.File.Path.FullPath, out var record) ? new SceneId(record.Settings.SourceIdentity) : null;
    }

    // Each change of a preview writes its file once the changes settle; the content is read now, on the scene's thread.
    private async void SaveLater(Document document, Preview preview)
    {
        if (this.previewFiles is null || this.disposed)
        {
            return;
        }

        var pending = preview.Keep(ModelPreviewFiles.Encode(preview.Content.Scene));
        try
        {
            await Task.Delay(SaveDelay, pending).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            // A newer change, a flush or the preview's release writes it instead.
            return;
        }

        this.WriteUnsaved(document, preview);
        await this.writing.ConfigureAwait(true);
    }

    // Queues the write of a preview's changes not written yet, after the writes before it.
    private void WriteUnsaved(Document? document, Preview preview)
    {
        if (document is not null && this.previewFiles is { } files && preview.TakeUnsaved() is { } content)
        {
            var path = ModelPreviewFiles.PathOf(document);
            this.writing = WriteAfterAsync(this.writing, files, path, content);
        }

        static async Task WriteAfterAsync(Task before, ModelPreviewFiles files, FileSystemPath path, byte[] content)
        {
            await before.ConfigureAwait(false);
            await files.WriteAsync(path, content).ConfigureAwait(false);
        }
    }

    // A preview that goes writes the changes it has not written yet.
    private void Release(Document? document, Preview preview)
    {
        this.WriteUnsaved(document, preview);
        preview.Dispose();
    }

    private void OnDocumentClosed(object? sender, DocumentEventArgs args)
    {
        if (this.previews.Remove(args.Document.Id, out var preview))
        {
            this.Release(args.Document, preview);
        }
    }

    // A preview follows its model's source when the source moves, and holds its changes until they are written.
    private sealed class Preview : IDisposable
    {
        private CancellationTokenSource? pendingSave;
        private byte[]? unsaved;

        public Preview(ModelContent model, SceneContent content)
        {
            this.Model = model;
            this.Content = content;
            model.Relocated += this.OnRelocated;
            content.Authoring.Changed += this.OnChanged;
        }

        public event EventHandler? Changed;

        public ModelContent Model { get; }

        public SceneContent Content { get; }

        // Holds the content to write, replacing any earlier one, and returns what cancels its wait.
        public CancellationToken Keep(byte[] content)
        {
            this.CancelPendingSave();
            this.unsaved = content;
            this.pendingSave = new CancellationTokenSource();
            return this.pendingSave.Token;
        }

        public byte[]? TakeUnsaved()
        {
            this.CancelPendingSave();
            var content = this.unsaved;
            this.unsaved = null;
            return content;
        }

        public void DiscardUnsaved() => _ = this.TakeUnsaved();

        public void Dispose()
        {
            this.CancelPendingSave();
            this.Model.Relocated -= this.OnRelocated;
            this.Content.Authoring.Changed -= this.OnChanged;
            this.Content.Dispose();
        }

        private void CancelPendingSave()
        {
            this.pendingSave?.Cancel();
            this.pendingSave?.Dispose();
            this.pendingSave = null;
        }

        private void OnChanged(object? sender, SceneChangedEventArgs args) => this.Changed?.Invoke(this, EventArgs.Empty);

        private void OnRelocated(object? sender, ModelRelocatedEventArgs args) => this.Content.FollowReferences(args.Relocation);
    }
}
