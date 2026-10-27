// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using DroidNet.Documents;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Models;
using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor;

/// <summary>
/// The kept import problems of the open models, for the Problems pane (model editor M-ED5): each model's last import
/// diagnostics, kept with its publication, read when its document opens and again after every cook.
/// </summary>
/// <remarks>
/// A model's problems are listed under the model's file. One that names a node of the model, by the name the source
/// gives it, selects that node when exactly one node of the last import has that name. The problems are read on the
/// thread that made this source, where the documents open and close.
/// </remarks>
public sealed class ModelImportProblems : IAssetProblemSource, IDisposable
{
    private readonly OpenDocuments documents;
    private readonly DocumentScenes scenes;
    private readonly ICookRunService runs;
    private readonly IPublishedDiagnostics published;
    private readonly IProjectSession session;
    private readonly SubscriberEvent<EventHandler> problemsChanged = new();
    private readonly Lock gate = new();
    private readonly SynchronizationContext? owner = SynchronizationContext.Current;
    private ImmutableDictionary<DocumentId, ImmutableArray<AssetProblem>> models = ImmutableDictionary<DocumentId, ImmutableArray<AssetProblem>>.Empty;
    private ImmutableArray<AssetProblem> problems = [];
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ModelImportProblems"/> class.</summary>
    /// <param name="documents">The session's open documents, among them the models.</param>
    /// <param name="scenes">The scene each open document shows, which names a model's nodes.</param>
    /// <param name="runs">The session's cook runs, after each of which the problems are read again.</param>
    /// <param name="published">Reads the diagnostics kept with a model's publication.</param>
    /// <param name="session">The project session.</param>
    public ModelImportProblems(OpenDocuments documents, DocumentScenes scenes, ICookRunService runs, IPublishedDiagnostics published, IProjectSession session)
    {
        ArgumentNullException.ThrowIfNull(documents);
        ArgumentNullException.ThrowIfNull(scenes);
        ArgumentNullException.ThrowIfNull(runs);
        ArgumentNullException.ThrowIfNull(published);
        ArgumentNullException.ThrowIfNull(session);
        this.documents = documents;
        this.scenes = scenes;
        this.runs = runs;
        this.published = published;
        this.session = session;
        documents.Opened += this.OnOpened;
        documents.Closed += this.OnClosed;
        runs.CookCompleted += this.OnCookCompletedAsync;
        foreach (var document in documents.Documents.Where(DocumentScenes.ShowsModel))
        {
            _ = this.ReadAsync(document);
        }
    }

    /// <inheritdoc/>
    public event EventHandler? ProblemsChanged
    {
        add => this.problemsChanged.Add(value);
        remove => this.problemsChanged.Remove(value);
    }

    /// <inheritdoc/>
    public IReadOnlyList<AssetProblem> Problems => this.problems;

    /// <summary>Gets the kept problems of one open model.</summary>
    /// <param name="document">The model's document.</param>
    /// <returns>The problems; empty for a document that is not an open model's or has none.</returns>
    public IReadOnlyList<AssetProblem> ProblemsOf(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return this.models.GetValueOrDefault(document.Id, []);
    }

    /// <summary>Reads an open model's kept problems again.</summary>
    /// <param name="document">The model's document.</param>
    /// <returns>A task that completes when the problems were read.</returns>
    public async Task ReadAsync(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        if (this.disposed || document.Content is not ModelContent model)
        {
            return;
        }

        var project = this.session.Context;
        ImmutableArray<DiagnosticRecord> kept;
        try
        {
            kept = await this.published.ReadDiagnosticsAsync(project, model.Source, this.session.Lifetime).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            // The project closed.
            return;
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            // An unreadable publication keeps the problems last read; the next cook reads them again.
            return;
        }

        if (this.disposed || !ReferenceEquals(this.session.Context, project) || !this.documents.Documents.Contains(document))
        {
            return;
        }

        this.Update(document.Id, kept.Select(issue => this.ProblemOf(document, model, issue)).ToImmutableArray());
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.documents.Opened -= this.OnOpened;
        this.documents.Closed -= this.OnClosed;
        this.runs.CookCompleted -= this.OnCookCompletedAsync;
    }

    // The node a diagnostic names by its source name: the one node of the last import with that name.
    private static ModelPublishedNode? NodeNamed(ModelImportSettings? record, string? name)
    {
        if (record is null || string.IsNullOrEmpty(name))
        {
            return null;
        }

        var named = record.PublishedNodes.Where(node => string.Equals(node.Anchor[(node.Anchor.LastIndexOf('/') + 1)..], name, StringComparison.Ordinal)).Take(2).ToArray();
        return named.Length == 1 ? named[0] : null;
    }

    // A problem is listed under its model's file; one that names a node selects it in the model's editor.
    private AssetProblem ProblemOf(Document document, ModelContent model, DiagnosticRecord issue)
    {
        var node = NodeNamed(model.Record, issue.AffectedEntity?.SourceObjectName);
        if (node is not null && this.scenes.ReferenceOf(document, NodeId.FromValue(node.NodeId)) is { } reference)
        {
            issue = issue with
            {
                AffectedVirtualPath = issue.AffectedVirtualPath ?? model.Source.VirtualPath,
                SuggestedAction = new PrimaryAction
                {
                    ActionId = "Model.SelectNode",
                    Label = "Select node",
                    Kind = PrimaryActionKind.Custom,
                    Payload = new Dictionary<string, string>(StringComparer.Ordinal)
                    {
                        ["AssetUri"] = model.Source.ToUriString(),
                        ["NodeId"] = node.NodeId.ToString("D"),
                        ["NodeReference"] = reference.ToString(),
                    },
                },
            };
        }

        return new CookProblem(issue) { File = document.File.Path.FullPath };
    }

    private void Update(DocumentId document, ImmutableArray<AssetProblem>? kept)
    {
        lock (this.gate)
        {
            this.models = kept is { } list ? this.models.SetItem(document, list) : this.models.Remove(document);
            this.problems = [.. this.models.Values.SelectMany(static list => list)];
        }

        this.problemsChanged.Invoke(handler => handler(this, EventArgs.Empty));
    }

    private void OnOpened(object? sender, DocumentEventArgs args)
    {
        if (DocumentScenes.ShowsModel(args.Document))
        {
            _ = this.ReadAsync(args.Document);
        }
    }

    private void OnClosed(object? sender, DocumentEventArgs args)
    {
        if (this.models.ContainsKey(args.Document.Id))
        {
            this.Update(args.Document.Id, kept: null);
        }
    }

    // The cook's publication waits for its consumers, so the problems are read after it, on the documents' thread.
    private Task OnCookCompletedAsync(CookCompletedEventArgs args)
    {
        if (this.owner is { } context)
        {
            context.Post(_ => this.ReadOpenModels(), state: null);
        }
        else
        {
            this.ReadOpenModels();
        }

        return Task.CompletedTask;
    }

    private void ReadOpenModels()
    {
        foreach (var document in this.documents.Documents.Where(DocumentScenes.ShowsModel).ToArray())
        {
            _ = this.ReadAsync(document);
        }
    }
}
