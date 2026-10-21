// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Input;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector;

/// <summary>
///     ViewModel for editing properties of selected SceneNode entities in the World Editor.
/// </summary>
public sealed partial class SceneNodeEditorViewModel : MultiSelectionDetails<SceneNode>, ISceneComponentEdits, IDisposable
{
    // The selection source of this inspector's "inspect this node" requests; the Scene Explorer reveals and focuses the row.
    private const string InspectorSelectionSource = "Inspector";

    private readonly ILogger logger;

    private readonly Dictionary<Type, IPropertyEditor<SceneNode>> editorInstances = [];
    private readonly Dictionary<Type, Func<IPropertyEditor<SceneNode>>> propertyEditorFactories;
    private readonly IAssetReveal assetReveal;
    private readonly IDocumentArea documentArea;
    private readonly IScenePreview scenePreview;
    private readonly SceneNodeInteraction? interaction;
    private readonly DispatcherQueue? dispatcher;
    private readonly InspectorSelectionObserver selectionObserver;
    private readonly EnvironmentViewModel environmentEditor;
    private readonly NodeRenderingViewModel renderingEditor;
    private readonly ISceneSelectionService sceneSelectionService;
    private (DocumentId document, string property)? pendingEnvironmentFocus;

    // The atmosphere source the inspector selected, whose directional light it shows once the selection arrives.
    private NodeId? pendingAtmosphereSource;

    private bool isDisposed;
    private Document? watchedDocument;
    private ICollection<SceneNode> items;
    private Scene? activeScene;

    // The document that edits the active scene; selection and scene lifetime messages name it.
    private DocumentId? activeDocumentId;
    private int pendingLiveSyncEditCount;

    // The classified row-kind context of the latest selection: the node list alone cannot say
    // whether the author selected the root, folders, or a mixed batch.
    private SceneSelectionContext selectionContext = SceneSelectionContext.Empty;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeEditorViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The hosting context for WinUI dispatching.</param>
    /// <param name="vmToViewConverter">The converter for resolving views from viewmodels.</param>
    /// <param name="assetReveal">What shows assets in the Content Browser.</param>
    /// <param name="documentArea">The window's document area, whose active scene document the inspector shows.</param>
    /// <param name="assetProvider">The shared asset identity and availability feed.</param>
    /// <param name="scenePreview">The engine preview, which reports the fields still waiting for the engine.</param>
    /// <param name="selectionService">The authoritative document selection owner, used to hydrate the typed context at startup.</param>
    /// <param name="materialSlots">The current native geometry slot inventories.</param>
    /// <param name="projectContexts">The active project lifetime.</param>
    /// <param name="inputSources">The project's input sources, which declare the scene's starting input contexts.</param>
    /// <param name="interaction">The shared editor lock owner.</param>
    /// <param name="scripts">The scripts, for script slot parameters and problems.</param>
    /// <param name="codeEditor">The code editor, for editing a slot's script.</param>
    /// <param name="scriptCreation">What creates a script for a slot, with New Script.</param>
    /// <param name="problems">What shows a script's problem in the Problems panel.</param>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the
    ///     recognition process. If <see langword="null" />, logging is disabled.
    /// </param>
    public SceneNodeEditorViewModel(
        HostingContext hosting,
        ViewModelToView vmToViewConverter,
        IAssetReveal assetReveal,
        IDocumentArea documentArea,
        IAssetFeed assetProvider,
        IScenePreview scenePreview,
        ISceneSelectionService selectionService,
        IGeometryMaterialSlotProvider materialSlots,
        IProjectContextService projectContexts,
        IInputSources inputSources,
        ILoggerFactory? loggerFactory = null,
        SceneNodeInteraction? interaction = null,
        IScripts? scripts = null,
        ICodeEditorService? codeEditor = null,
        IScriptCreation? scriptCreation = null,
        IProblemReveal? problems = null)
        : base(loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<SceneNodeEditorViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<SceneNodeEditorViewModel>();
        this.LoggerFactory = loggerFactory;

        this.assetReveal = assetReveal;
        this.documentArea = documentArea;
        this.scenePreview = scenePreview;
        this.interaction = interaction;
        this.sceneSelectionService = selectionService;
        this.VmToViewConverter = vmToViewConverter;
        this.dispatcher = hosting.Dispatcher;
        this.scenePreview.PendingFieldsChanged += this.OnPendingFieldsChanged;
        this.sceneSelectionService.PropertyFocusRequested += this.OnPropertyFocusRequested;
        this.scenePreview.ReadyChanged += this.OnPreviewReadyChanged;

        this.selectionObserver = new(this.dispatcher, this.OnSelectionObserved);
        this.propertyEditorFactories = InspectorEditorFactory.Create(
            hosting,
            assetProvider,
            materialSlots,
            projectContexts,
            loggerFactory,
            this.AuthoringOf,
            new InspectorScriptServices(scripts, codeEditor, scriptCreation, problems));
        this.environmentEditor = new EnvironmentViewModel(
            this.AuthoringOf,
            assetProvider,
            this.InspectAtmosphereSource,
            hosting.DispatcherScheduler,
            this.ObserveSkyLightAsync,
            context => StartingContextRules.Describe(projectContexts.ActiveProject, context, inputSources.Find),
            asset => this.RevealAssetAsync(projectContexts, asset),
            projectContexts,
            inputSources);
        this.renderingEditor = new NodeRenderingViewModel(this.AuthoringOf);

        // Startup hydrates the authoritative typed context with the nodes: a folder-only selection
        // must initialize as the grouping summary, not as "scene properties".
        var activeDocument = this.ActiveSceneDocumentId;
        this.activeScene = this.ResolveDocumentScene(activeDocument);
        this.selectionContext = this.sceneSelectionService.GetContext(activeDocument ?? DocumentId.Empty);
        this.items = this.activeScene is { } shownScene && activeDocument is { } shownDocument && !this.HasSelectionSummary
            ? this.sceneSelectionService.GetSelectedNodes(shownDocument, shownScene).ToList()
            : [];
        this.activeDocumentId = this.items.Count == 0 ? null : activeDocument;

        if (interaction is { } interactionService)
        {
            interactionService.StateChanged += this.OnWorkspaceInteractionStateChanged;
        }

        this.NotifyLockEligibilityChanged();
        this.RefreshPendingLiveSyncState();
        this.environmentEditor.SetScene(this.HasEnvironmentView ? this.activeScene : null);
        this.UpdateItemsCollection(this.items);
        this.SubscribeToComponentCollections();
        this.LogConstructed(this.items.Count);

        this.RegisterSceneSubscriptions();
    }

    /// <summary>Gets the scene-level editor hosted outside component-property scrolling.</summary>
    public EnvironmentViewModel EnvironmentEditor => this.environmentEditor;

    /// <summary>
    /// Gets a value indicating whether exactly one item is selected.
    /// </summary>
    public bool IsSingleItemSelected => this.items.Count == 1;

    /// <summary>
    /// Gets the single selected node, or <see langword="null"/> when the selection is empty or contains multiple nodes.
    /// Intended for binding into <see cref="SceneNodeDetailsView"/>.
    /// </summary>
    public SceneNode? SelectedNode => this.items.Count == 1 ? this.items.First() : null;

    /// <summary>
    /// Gets a value indicating whether the inspector has node or scene-level content to show.
    /// </summary>
    public bool HasInspectorContent => this.HasItems || this.activeScene is not null;

    /// <summary>Gets the inspector identity for the current scene or node selection.</summary>
    public string InspectorTitle => this.selectionContext.Kind switch
    {
        SceneSelectionKind.Folder => "Grouping Summary",
        SceneSelectionKind.Mixed => "Selection Summary",
        SceneSelectionKind.Node => "Component Inspector",
        _ => this.HasItems ? "Component Inspector" : "Scene Inspector",
    };

    /// <summary>
    /// Gets a value indicating whether the selection is folders or a mixed batch, which shows a
    /// grouping summary instead of component editors or the environment editor. Folder and mixed
    /// selections must never be reduced to their node subset and presented as a component edit.
    /// </summary>
    public bool HasSelectionSummary => this.selectionContext.Kind is SceneSelectionKind.Folder or SceneSelectionKind.Mixed;

    /// <summary>Gets a value indicating whether a grouping summary or unresolved selection notice is visible.</summary>
    public bool HasSelectionNotice => this.HasSelectionSummary
        || (this.selectionContext.Kind == SceneSelectionKind.Node && !this.HasItems);

    /// <summary>Gets a value indicating whether the current node selection can be authored.</summary>
    public bool CanEditSelectedNodes => this.items.All(node => this.interaction?.GetLockOwner(node) is null);

    /// <summary>Gets a value indicating whether a lock protects any node in the current selection.</summary>
    public bool HasLockedSelection => this.items.Any(node => this.interaction?.GetLockOwner(node) is not null);

    /// <summary>Gets the explanation shown when a selected node is protected by a lock.</summary>
    public string LockedEditingMessage
    {
        get
        {
            var node = this.items.FirstOrDefault(candidate => this.interaction?.GetLockOwner(candidate) is not null);
            if (node is null || this.interaction?.GetLockOwner(node) is not { } lockOwner)
            {
                return string.Empty;
            }

            return ReferenceEquals(node, lockOwner)
                ? $"'{node.Name}' is locked. It remains selectable for inspection, but its properties cannot be changed."
                : $"'{node.Name}' is protected by locked parent '{lockOwner.Name}'. Its properties cannot be changed.";
        }
    }

    /// <summary>Gets a value indicating whether the scene environment editor is the active surface.</summary>
    public bool HasEnvironmentView => !this.HasItems
        && this.selectionContext.Kind is SceneSelectionKind.Empty or SceneSelectionKind.Scene
        && this.activeScene is not null;

    /// <summary>Gets the aggregate description shown for folder and mixed selections.</summary>
    public string SelectionSummaryText => this.selectionContext.Kind switch
    {
        SceneSelectionKind.Folder => this.selectionContext.SelectedFolderIds.Count == 1
            ? "1 grouping selected. Select a contained object to edit its components."
            : $"{this.selectionContext.SelectedFolderIds.Count} groupings selected. Select a contained object to edit its components.",
        SceneSelectionKind.Mixed => $"{this.selectionContext.SelectedNodeIds.Count} objects and {this.selectionContext.SelectedFolderIds.Count} groupings selected.",
        SceneSelectionKind.Node when !this.HasItems => "The selected objects are not available in the current scene.",
        _ => string.Empty,
    };

    /// <summary>
    /// Gets the number of scene property edits buffered until the runtime can replay them.
    /// </summary>
    public int PendingLiveSyncEditCount
    {
        get => this.pendingLiveSyncEditCount;
        private set
        {
            if (!this.SetProperty(ref this.pendingLiveSyncEditCount, value))
            {
                return;
            }

            this.OnPropertyChanged(nameof(this.HasPendingLiveSyncEdits));
            this.OnPropertyChanged(nameof(this.PendingLiveSyncMessage));
        }
    }

    /// <summary>
    /// Gets a value indicating whether the editor should show the pending live-sync banner.
    /// </summary>
    public bool HasPendingLiveSyncEdits => this.PendingLiveSyncEditCount > 0;

    /// <summary>
    /// Gets the title displayed in the pending live-sync banner.
    /// </summary>
    public string PendingLiveSyncTitle { get; } = "Runtime sync pending";

    /// <summary>
    /// Gets the status text displayed in the pending live-sync banner.
    /// </summary>
    public string PendingLiveSyncMessage
        => this.PendingLiveSyncEditCount == 1
            ? "1 editor property edit will replay after the scene syncs."
            : string.Create(System.Globalization.CultureInfo.CurrentCulture, $"{this.PendingLiveSyncEditCount} editor property edits will replay after the scene syncs.");

    /// <summary>
    /// Gets the <see cref="ILoggerFactory"/> used by this view model for creating loggers.
    /// The factory is provided via the constructor and may be <see langword="null"/>; when <see langword="null"/>
    /// a <see cref="NullLoggerFactory"/> is used internally to disable logging.
    /// </summary>
    public ILoggerFactory? LoggerFactory { get; private set; }

    /// <summary>
    ///     Gets a viewmodel to view converter provided by the local Ioc container, which can resolve
    ///     view from viewmodels registered locally. This converter must be used instead of the default
    ///     Application converter.
    /// </summary>
    public ViewModelToView VmToViewConverter { get; }

    /// <summary>
    /// Gets the undo/redo history for the currently selected scene document.
    /// </summary>
    public HistoryKeeper History => this.DocumentOf(this.items.FirstOrDefault()?.Scene ?? this.activeScene)?.History ?? UndoRedo.Default[this];

    /// <inheritdoc />
    public async Task AddComponentAsync(SceneNode node, string kind)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(kind);
        if (this.AuthoringOf(node.Scene) is { } authoring)
        {
            _ = await authoring.ExecuteAsync(SceneComponents.Add(authoring, node.Id, kind)).ConfigureAwait(true);
        }
    }

    /// <inheritdoc />
    public async Task RemoveComponentAsync(SceneNode node, GameComponent component)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(component);
        if (this.AuthoringOf(node.Scene) is { } authoring)
        {
            _ = await authoring.ExecuteAsync(new RemoveComponent(node.Id, component.Id)).ConfigureAwait(true);
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.scenePreview.PendingFieldsChanged -= this.OnPendingFieldsChanged;
        this.sceneSelectionService.PropertyFocusRequested -= this.OnPropertyFocusRequested;
        this.sceneSelectionService.SelectionChanged -= this.OnSelectionServiceChanged;
        this.scenePreview.ReadyChanged -= this.OnPreviewReadyChanged;
        this.documentArea.ActiveDocumentChanged -= this.OnActiveDocumentChanged;
        this.WatchContent(document: null);
        if (this.interaction is { } interactionService)
        {
            interactionService.StateChanged -= this.OnWorkspaceInteractionStateChanged;
        }

        this.selectionObserver.Dispose();
        this.StopObservingComponentFeedback();
        foreach (var editor in this.editorInstances.Values.OfType<IDisposable>())
        {
            editor.Dispose();
        }

        this.environmentEditor.Dispose();
        this.renderingEditor.Dispose();
        this.editorInstances.Clear();
        this.boundEditors.Clear();
        this.LogDisposed();

        this.isDisposed = true;
    }

    /// <summary>Undoes the inspected scene's last edit through its authoring.</summary>
    /// <returns>Completion of the undo.</returns>
    internal Task UndoAsync() => this.AuthoringOf() is { } authoring ? authoring.UndoAsync() : Task.CompletedTask;

    /// <summary>Redoes the inspected scene's last undone edit through its authoring.</summary>
    /// <returns>Completion of the redo.</returns>
    internal Task RedoAsync() => this.AuthoringOf() is { } authoring ? authoring.RedoAsync() : Task.CompletedTask;

    /// <inheritdoc/>
    protected override void RefreshOwnProperties()
    {
        try
        {
            base.RefreshOwnProperties();
        }
        catch (Exception ex)
        {
            this.LogUnexpectedException("RefreshOwnProperties", ex);
            throw;
        }

        this.OnPropertyChanged(nameof(this.IsSingleItemSelected));
        this.OnPropertyChanged(nameof(this.SelectedNode));
        this.NotifyLockEligibilityChanged();
        this.OnPropertyChanged(nameof(this.HasInspectorContent));
        this.OnPropertyChanged(nameof(this.InspectorTitle));
        this.NotifySelectionRoutingChanged();
        this.RefreshPendingLiveSyncState();
    }

    /// <inheritdoc/>
    protected override ICollection<IPropertyEditor<SceneNode>> FilterPropertyEditors()
    {
        Debug.WriteLine($"[SceneNodeEditorViewModel] FilterPropertyEditors called. Items count: {this.items.Count}");
        var filteredEditors = new Dictionary<Type, IPropertyEditor<SceneNode>>();
        var result = new List<IPropertyEditor<SceneNode>>();

        this.environmentEditor.SetScene(this.HasEnvironmentView ? this.activeScene : null);
        if (this.HasEnvironmentView)
        {
            result.Add(this.environmentEditor);
        }

        var keysToCheck = this.GetApplicablePropertyEditorTypes();
        this.ReconcileComponentFilters();
        if (this.selectedComponentType is { } selected)
        {
            keysToCheck.IntersectWith([selected]);
        }

        if (this.items.Count == 0)
        {
            return result;
        }

        Debug.WriteLine($"[SceneNodeEditorViewModel] Keys to check after filtering: {string.Join(", ", keysToCheck.Select(k => k.Name))}");
        this.AddApplicablePropertyEditors(filteredEditors, keysToCheck);

        var before = this.propertyEditorFactories.Count;
        var after = filteredEditors.Count;
        this.LogFiltered(before, after);

        result.AddRange(filteredEditors.Values);

        // Node rendering flags follow every component section, in the unfiltered view only, and
        // only for nodes the flags affect.
        if (this.selectedComponentType is null && NodeRenderingViewModel.AppliesTo(this.items))
        {
            result.Add(this.renderingEditor);
        }

        return result;
    }

    /// <inheritdoc />
    protected override void DeactivatePropertyEditor(IPropertyEditor<SceneNode> editor)
        => ((ComponentPropertyEditor)editor).SetInputEnabled(enabled: false);

    /// <inheritdoc />
    protected override void UpdatePropertyEditorsValues()
    {
        var selection = this.items.ToArray();
        foreach (var editor in this.editorInstances.Values.Cast<ComponentPropertyEditor>().ToArray())
        {
            editor.UpdateValues(selection);
            _ = this.boundEditors.Add(editor);
            editor.SetInputEnabled(this.IsEditorInputEnabled(editor));
        }

        this.environmentEditor.UpdateValues(selection);
        this.environmentEditor.SetInputEnabled(this.PropertyEditors.Contains(this.environmentEditor));
        this.renderingEditor.UpdateValues(selection);
        this.renderingEditor.SetInputEnabled(this.IsEditorInputEnabled(this.renderingEditor));
    }

    /// <summary>Raises the change notifications for the selection-kind routing surface.</summary>
    private void NotifySelectionRoutingChanged()
    {
        this.OnPropertyChanged(nameof(this.HasSelectionSummary));
        this.OnPropertyChanged(nameof(this.HasSelectionNotice));
        this.OnPropertyChanged(nameof(this.HasEnvironmentView));
        this.OnPropertyChanged(nameof(this.SelectionSummaryText));
        this.OnPropertyChanged(nameof(this.InspectorTitle));
    }

    private Scene? ResolveDocumentScene(DocumentId? documentId)
        => documentId is { } id && this.documentArea.Documents.FirstOrDefault(document => document.Id == id)?.Content is SceneContent content
            ? content.Scene
            : null;

    private Task<SkyLightObservation?> ObserveSkyLightAsync(Scene scene, CancellationToken cancellationToken)
        => this.DocumentOf(scene) is { } document
            ? this.scenePreview.ObserveSkyLightAsync(document.Id, cancellationToken)
            : Task.FromResult<SkyLightObservation?>(null);

    private bool IsEditorInputEnabled(ComponentPropertyEditor editor)
        => this.PropertyEditors.Contains(editor) && this.CanEditSelectedNodes;

    // Ctrl+B's navigation: the Content Browser shows and selects the asset.
    private async Task RevealAssetAsync(IProjectContextService projectContexts, AssetPath asset)
    {
        if (projectContexts.ActiveProject is null)
        {
            return;
        }

        _ = await this.assetReveal.RevealAsync([asset]).ConfigureAwait(true);
    }

    private async Task InspectAtmosphereSource(NodeId nodeId)
    {
        var node = this.activeScene?.AllNodes.FirstOrDefault(candidate => candidate.Id == nodeId);
        if (node is null)
        {
            return;
        }

        if (this.ActiveSceneDocumentId is not { } documentId)
        {
            throw new InvalidOperationException("The atmosphere source could not be revealed in Scene Explorer.");
        }

        // The Explorer reveals and focuses the row for a selection written by this source; the selection may reach the
        // inspector later, through its dispatcher.
        this.pendingAtmosphereSource = node.Id;
        this.sceneSelectionService.SetSelection(documentId, [node], InspectorSelectionSource);
        this.ApplyPendingAtmosphereFilter();
    }

    private void ApplyPendingAtmosphereFilter()
    {
        if (this.pendingAtmosphereSource is not { } source || this.items.Count != 1 || this.items.First().Id != source)
        {
            return;
        }

        this.pendingAtmosphereSource = null;
        this.SetComponentFilter(typeof(DirectionalLightComponent));
        this.RefreshPropertyEditors(refreshValues: false);
        this.RefreshEditorInputState();
    }

    private HashSet<Type> GetApplicablePropertyEditorTypes()
    {
        var keysToCheck = new HashSet<Type>(this.propertyEditorFactories.Keys);
        Debug.WriteLine($"[SceneNodeEditorViewModel] propertyEditorFactories has {this.propertyEditorFactories.Count} factories: {string.Join(", ", this.propertyEditorFactories.Keys.Select(k => k.Name))}");

        foreach (var entity in this.items)
        {
            Debug.WriteLine($"[SceneNodeEditorViewModel] Checking entity: {entity.Name}, Components: {string.Join(", ", entity.Components.Select(c => c.GetType().Name))}");
            foreach (var key in keysToCheck.ToList()
                         .Where(key => entity.Components.All(component => component.GetType() != key)))
            {
                Debug.WriteLine($"[SceneNodeEditorViewModel] Removing key {key.Name} (component not found on entity)");
                _ = keysToCheck.Remove(key);
            }
        }

        return keysToCheck;
    }

    private void AddApplicablePropertyEditors(
        Dictionary<Type, IPropertyEditor<SceneNode>> filteredEditors,
        HashSet<Type> keysToCheck)
    {
        foreach (var kvp in this.propertyEditorFactories)
        {
            if (!keysToCheck.Contains(kvp.Key))
            {
                continue;
            }

            filteredEditors[kvp.Key] = this.GetOrCreatePropertyEditor(kvp);
        }
    }

    private IPropertyEditor<SceneNode> GetOrCreatePropertyEditor(
        KeyValuePair<Type, Func<IPropertyEditor<SceneNode>>> factory)
    {
        if (this.editorInstances.TryGetValue(factory.Key, out var instance))
        {
            Debug.WriteLine($"[SceneNodeEditorViewModel] Reusing existing editor instance for {factory.Key.Name}");
            return instance;
        }

        Debug.WriteLine($"[SceneNodeEditorViewModel] Creating NEW editor instance for {factory.Key.Name}");
        instance = factory.Value();
        ((ComponentPropertyEditor)instance).SetInputEnabled(enabled: false);
        this.editorInstances[factory.Key] = instance;
        this.ObserveComponentFeedback(factory.Key, (ComponentPropertyEditor)instance);
        return instance;
    }

    private void RegisterSceneSubscriptions()
    {
        this.documentArea.ActiveDocumentChanged += this.OnActiveDocumentChanged;
        this.WatchContent(this.documentArea.ActiveDocument);
        this.sceneSelectionService.SelectionChanged += this.OnSelectionServiceChanged;
    }

    // A scene document that becomes active, or reloads while active, hydrates the inspector from its scene.
    private void OnActiveDocumentChanged(object? sender, EventArgs args)
    {
        if (this.isDisposed)
        {
            return;
        }

        // The watched scene document closed: the inspector has nothing to show for it.
        if (this.watchedDocument is { } watched && !this.documentArea.Documents.Contains(watched))
        {
            this.ClearClosedDocument(watched.Id);
        }

        if (this.documentArea.ActiveDocument is not { Content: SceneContent } active || ReferenceEquals(active, this.watchedDocument))
        {
            return;
        }

        this.WatchContent(active);
        this.ShowDocument(active);
    }

    private void WatchContent(Document? document)
    {
        if (this.watchedDocument is { } previous)
        {
            previous.ContentReplaced -= this.OnContentReplaced;
        }

        this.watchedDocument = document is { Content: SceneContent } ? document : null;
        if (this.watchedDocument is { } current)
        {
            current.ContentReplaced += this.OnContentReplaced;
        }
    }

    private void OnContentReplaced(object? sender, DocumentContentReplacedEventArgs args)
    {
        if (!this.isDisposed && sender is Document document && ReferenceEquals(this.documentArea.ActiveDocument, document))
        {
            this.ShowDocument(document);
        }
    }

    private void ShowDocument(Document document)
    {
        var scene = SceneContent.Of(document).Scene;
        if (this.items.Any(node => !ReferenceEquals(node.Scene, scene)))
        {
            this.items = [];
        }

        // A fresh document session hydrates the authoritative typed context for that
        // document: a folder selection reopens as the grouping summary, not scene properties.
        this.selectionContext = this.sceneSelectionService.GetContext(document.Id);
        this.items = this.HasSelectionSummary ? [] : this.sceneSelectionService.GetSelectedNodes(document.Id, scene).ToList();

        this.activeScene = scene;
        this.activeDocumentId = document.Id;
        this.RefreshPendingLiveSyncState();
        this.environmentEditor.SetScene(this.HasEnvironmentView ? scene : null);
        this.NotifySelectionRoutingChanged();
        this.UpdateItemsCollection(this.items);
        this.SubscribeToComponentCollections();
        this.ApplyPendingEnvironmentFocus();
    }

    private void ClearClosedDocument(DocumentId documentId)
    {
        if (this.activeDocumentId != documentId && this.watchedDocument?.Id != documentId)
        {
            return;
        }

        if (this.watchedDocument?.Id == documentId)
        {
            this.WatchContent(document: null);
        }

        this.items = [];
        this.SubscribeToComponentCollections();
        this.activeScene = null;
        this.activeDocumentId = null;
        this.selectionContext = this.sceneSelectionService.GetContext(DocumentId.Empty);
        this.RefreshPendingLiveSyncState();
        this.environmentEditor.SetScene(value: null);
        this.NotifySelectionRoutingChanged();
        this.UpdateItemsCollection(this.items);
    }

    // A selection of the active scene document written by any source (the explorer, the viewport, an inspector request).
    private void OnSelectionServiceChanged(object? sender, SceneSelectionChangedEventArgs args)
    {
        _ = sender;
        if (this.isDisposed)
        {
            return;
        }

        if (this.dispatcher is { } queue)
        {
            _ = queue.DispatchAsync(() => this.ApplySelection(args));
        }
        else
        {
            this.ApplySelection(args);
        }
    }

    private void ApplySelection(SceneSelectionChangedEventArgs args)
    {
        if (this.isDisposed
            || this.ActiveSceneDocumentId != args.DocumentId
            || !ReferenceEquals(this.sceneSelectionService.GetContext(args.DocumentId), args.Context))
        {
            return;
        }

        this.selectionContext = args.Context;

        // Folder and mixed batches show the grouping summary: their node subset must
        // not populate component editors as if the whole selection were nodes.
        var scene = this.ResolveDocumentScene(args.DocumentId);
        this.items = this.HasSelectionSummary || scene is null
            ? []
            : this.sceneSelectionService.GetSelectedNodes(args.DocumentId, scene).ToList();
        if (this.items.FirstOrDefault()?.Scene is { } selectedScene)
        {
            this.activeScene = selectedScene;
            this.activeDocumentId = args.DocumentId;
        }

        this.RefreshPendingLiveSyncState();
        this.environmentEditor.SetScene(this.HasEnvironmentView ? this.activeScene : null);
        this.NotifySelectionRoutingChanged();
        this.LogSelectionChanged(this.items.Count);
        this.UpdateItemsCollection(this.items);
        this.SubscribeToComponentCollections();
        this.ApplyPendingAtmosphereFilter();
    }

    private void ApplyPendingEnvironmentFocus()
    {
        if (this.pendingEnvironmentFocus is not { } target || this.activeScene is null || this.activeDocumentId != target.document)
        {
            return;
        }

        this.pendingEnvironmentFocus = null;
        this.items = [];
        this.environmentEditor.SetScene(this.activeScene);
        this.UpdateItemsCollection(this.items);
        this.environmentEditor.RequestFieldFocus(target.property);
    }

    // The scene's properties show once the scene is the shown one; until then the request waits.
    private void OnPropertyFocusRequested(object? sender, ScenePropertyFocusEventArgs args)
    {
        this.pendingEnvironmentFocus = (args.DocumentId, args.PropertyPath);
        this.ApplyPendingEnvironmentFocus();
    }

    private SceneAuthoring? AuthoringOf()
        => this.AuthoringOf(this.items.FirstOrDefault()?.Scene ?? this.activeScene);

    private DocumentId? ActiveSceneDocumentId
        => this.documentArea.ActiveDocument is { Content: SceneContent } active ? active.Id : null;

    private Document? DocumentOf(Scene? scene)
        => scene is null ? null : this.documentArea.Documents.FirstOrDefault(document => document.Content is SceneContent content && ReferenceEquals(content.Scene, scene));

    private SceneAuthoring? AuthoringOf(Scene? scene)
        => (this.DocumentOf(scene)?.Content as SceneContent)?.Authoring;

    private void OnPendingFieldsChanged(object? sender, ScenePreviewEventArgs e)
    {
        if (!this.isDisposed && this.activeDocumentId == e.Document)
        {
            this.PendingLiveSyncEditCount = this.scenePreview.PendingFieldCount(e.Document);
        }
    }

    private void OnPreviewReadyChanged(object? sender, ScenePreviewEventArgs e)
    {
        if (!this.isDisposed && this.activeDocumentId == e.Document)
        {
            this.RefreshPendingLiveSyncState();
        }
    }

    private void OnWorkspaceInteractionStateChanged(object? sender, EventArgs args)
    {
        _ = sender;
        _ = args;

        void UpdateEligibility()
        {
            if (this.isDisposed)
            {
                return;
            }

            this.NotifyLockEligibilityChanged();
            this.RefreshEditorInputState();
        }

        if (this.dispatcher is { HasThreadAccess: false })
        {
            _ = this.dispatcher.TryEnqueue(UpdateEligibility);
            return;
        }

        UpdateEligibility();
    }

    private void NotifyLockEligibilityChanged()
    {
        this.OnPropertyChanged(nameof(this.CanEditSelectedNodes));
        this.OnPropertyChanged(nameof(this.HasLockedSelection));
        this.OnPropertyChanged(nameof(this.LockedEditingMessage));
    }

    private void RefreshPendingLiveSyncState()
        => this.PendingLiveSyncEditCount = this.activeDocumentId is { } document
            ? this.scenePreview.PendingFieldCount(document)
            : 0;

    private void SubscribeToComponentCollections()
        => this.selectionObserver.Bind(this.items);

    private void OnSelectionObserved(InspectorSelectionChange change)
    {
        if (change == InspectorSelectionChange.Structure)
        {
            this.UpdateItemsCollection(this.items);
            this.SubscribeToComponentCollections();
        }
        else
        {
            this.RefreshPropertyEditorValues();
        }
    }
}
