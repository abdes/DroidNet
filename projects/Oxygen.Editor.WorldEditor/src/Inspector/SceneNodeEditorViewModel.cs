// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
///     ViewModel for editing properties of selected SceneNode entities in the World Editor.
/// </summary>
public sealed partial class SceneNodeEditorViewModel : MultiSelectionDetails<SceneNode>, IDisposable
{
    private readonly ILogger logger;

    private readonly Dictionary<Type, IPropertyEditor<SceneNode>> editorInstances = [];
    private readonly Dictionary<Type, Func<IMessenger?, IPropertyEditor<SceneNode>>> propertyEditorFactories;
    private readonly IMessenger messenger;
    private readonly ISceneDocumentCommandService commandService;
    private readonly IDocumentService documentService;
    private readonly ISceneEngineSync sceneEngineSync;
    private readonly WorkspaceInteractionService? interaction;
    private readonly WindowId windowId;
    private readonly DispatcherQueue? dispatcher;
    private readonly InspectorSelectionObserver selectionObserver;
    private readonly EnvironmentViewModel environmentEditor;
    private readonly NodeRenderingViewModel renderingEditor;
    private readonly ISceneSelectionService sceneSelectionService;
    private (Guid sceneId, string property)? pendingEnvironmentFocus;

    private bool isDisposed;
    private ICollection<SceneNode> items;
    private Scene? activeScene;
    private int pendingLiveSyncEditCount;

    // The classified row-kind context of the latest selection: the node list alone cannot say
    // whether the author selected the root, folders, or a mixed batch.
    private SceneSelectionContext selectionContext = SceneSelectionContext.Empty;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeEditorViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The hosting context for WinUI dispatching.</param>
    /// <param name="vmToViewConverter">The converter for resolving views from viewmodels.</param>
    /// <param name="messenger">The messenger for MVVM messaging.</param>
    /// <param name="commandService">The scene document command service.</param>
    /// <param name="documentService">The document service used by scene commands.</param>
    /// <param name="windowId">The WinUI window id used for document operations.</param>
    /// <param name="assetProvider">The shared asset identity and availability feed.</param>
    /// <param name="materialPickerService">The material picker service for geometry material slots.</param>
    /// <param name="sceneEngineSync">The scene engine-sync service that reports buffered live-sync work.</param>
    /// <param name="selectionService">The authoritative document selection owner, used to hydrate the typed context at startup.</param>
    /// <param name="builtins">The shared native catalog for engine choices.</param>
    /// <param name="contentDemand">The saved-asset preview request owner.</param>
    /// <param name="materialSlots">The current native geometry slot inventories.</param>
    /// <param name="projectContexts">The active project lifetime.</param>
    /// <param name="interaction">The shared editor lock owner.</param>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the
    ///     recognition process. If <see langword="null" />, logging is disabled.
    /// </param>
    public SceneNodeEditorViewModel(
        HostingContext hosting,
        ViewModelToView vmToViewConverter,
        IMessenger messenger,
        ISceneDocumentCommandService commandService,
        IDocumentService documentService,
        WindowId windowId,
        IContentBrowserAssetProvider assetProvider,
        IMaterialPickerService materialPickerService,
        ISceneEngineSync sceneEngineSync,
        ISceneSelectionService selectionService,
        IBuiltinCatalogDiscovery builtins,
        ISceneContentDemandService contentDemand,
        IGeometryMaterialSlotProvider materialSlots,
        IProjectContextService projectContexts,
        ILoggerFactory? loggerFactory = null,
        WorkspaceInteractionService? interaction = null)
        : base(loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<SceneNodeEditorViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<SceneNodeEditorViewModel>();
        this.LoggerFactory = loggerFactory;

        this.messenger = messenger;
        this.commandService = commandService;
        this.documentService = documentService;
        this.sceneEngineSync = sceneEngineSync;
        this.interaction = interaction;
        this.sceneSelectionService = selectionService;
        this.windowId = windowId;
        this.VmToViewConverter = vmToViewConverter;
        this.dispatcher = hosting.Dispatcher;
        this.sceneEngineSync.PendingPropertySyncCountChanged += this.OnPendingPropertySyncCountChanged;

        this.selectionObserver = new(this.dispatcher, this.OnSelectionObserved);
        this.propertyEditorFactories = InspectorEditorFactory.Create(
            hosting,
            assetProvider,
            materialPickerService,
            builtins,
            contentDemand,
            materialSlots,
            projectContexts,
            loggerFactory,
            commandService,
            this.CreateCommandContext);
        this.environmentEditor = new EnvironmentViewModel(commandService, this.CreateCommandContext, assetProvider, this.InspectAtmosphereSource, hosting.DispatcherScheduler);
        this.renderingEditor = new NodeRenderingViewModel(commandService, this.CreateCommandContext);

        this.items = this.messenger.Send(new SceneNodeSelectionRequestMessage()).SelectedEntities;
        this.activeScene = this.items.FirstOrDefault()?.Scene;

        // The request reply carries nodes only; startup must additionally hydrate the authoritative
        // typed context, or a folder-only selection initializes as "scene properties".
        var activeDocument = this.documentService.GetActiveDocumentId(this.windowId);
        this.selectionContext = this.sceneSelectionService.GetContext(activeDocument ?? Guid.Empty);
        if (this.HasSelectionSummary)
        {
            this.items = [];
        }

        this.activeScene ??= this.ResolveDocumentScene(activeDocument);

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

        this.RegisterSceneMessages(hosting);
        this.RegisterComponentMessages(hosting);
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
    public HistoryKeeper History => this.CreateCommandContext()?.History ?? UndoRedo.Default[this];

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.sceneEngineSync.PendingPropertySyncCountChanged -= this.OnPendingPropertySyncCountChanged;
        if (this.interaction is { } interactionService)
        {
            interactionService.StateChanged -= this.OnWorkspaceInteractionStateChanged;
        }

        this.messenger.UnregisterAll(this);
        WeakReferenceMessenger.Default.UnregisterAll(this);
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

    /// <summary>Focuses a scene environment property once its document is active and loaded.</summary>
    /// <param name="sceneId">The exact scene to inspect.</param>
    /// <param name="property">The stable environment property path.</param>
    public void FocusEnvironmentField(Guid sceneId, string property)
    {
        this.pendingEnvironmentFocus = (sceneId, property);
        this.ApplyPendingEnvironmentFocus();
    }

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

        // Node rendering flags follow every component section, in the unfiltered view only.
        if (this.selectedComponentType is null)
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

    private Scene? ResolveDocumentScene(Guid? documentId)
    {
        var metadata = this.documentService.GetOpenDocuments(this.windowId).OfType<SceneDocumentMetadata>()
            .FirstOrDefault(document => document.DocumentId == documentId);
        return metadata is null ? null : this.sceneEngineSync.GetDocumentScene(metadata);
    }

    private bool IsEditorInputEnabled(ComponentPropertyEditor editor)
        => this.PropertyEditors.Contains(editor) && this.CanEditSelectedNodes;

    private async Task InspectAtmosphereSource(Guid nodeId)
    {
        var node = this.activeScene?.AllNodes.FirstOrDefault(candidate => candidate.Id == nodeId);
        if (node is null)
        {
            return;
        }

        var request = this.messenger.Send(new InspectSceneNodeMessage(nodeId, this.windowId));
        if (!request.HasReceivedResponse || !await request.Response.ConfigureAwait(true))
        {
            throw new InvalidOperationException("The atmosphere source could not be revealed in Scene Explorer.");
        }

        if (this.items.Count == 1 && ReferenceEquals(this.items.First(), node))
        {
            this.SetComponentFilter(typeof(DirectionalLightComponent));
            this.RefreshPropertyEditors(refreshValues: false);
            this.RefreshEditorInputState();
        }
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
        KeyValuePair<Type, Func<IMessenger?, IPropertyEditor<SceneNode>>> factory)
    {
        if (this.editorInstances.TryGetValue(factory.Key, out var instance))
        {
            Debug.WriteLine($"[SceneNodeEditorViewModel] Reusing existing editor instance for {factory.Key.Name}");
            return instance;
        }

        Debug.WriteLine($"[SceneNodeEditorViewModel] Creating NEW editor instance for {factory.Key.Name}");
        instance = factory.Value(this.messenger);
        ((ComponentPropertyEditor)instance).SetInputEnabled(enabled: false);
        this.editorInstances[factory.Key] = instance;
        this.ObserveComponentFeedback(factory.Key, (ComponentPropertyEditor)instance);
        return instance;
    }

    private void RegisterSceneMessages(HostingContext hosting)
    {
        this.messenger.Register<SceneAuthoringUnloadedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() =>
            {
                if (this.isDisposed || this.activeScene?.Id != message.DocumentId)
                {
                    return;
                }

                this.items = [];
                this.SubscribeToComponentCollections();
                this.activeScene = null;
                this.selectionContext = this.sceneSelectionService.GetContext(Guid.Empty);
                this.RefreshPendingLiveSyncState();
                this.environmentEditor.SetScene(value: null);
                this.NotifySelectionRoutingChanged();
                this.UpdateItemsCollection(this.items);
            }));

        this.messenger.Register<SceneAuthoringLoadedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() =>
            {
                if (this.documentService.GetActiveDocumentId(this.windowId) != message.Metadata.DocumentId
                    || !this.documentService.GetOpenDocuments(this.windowId).Any(document => ReferenceEquals(document, message.Metadata)))
                {
                    return;
                }

                if (this.items.Any(node => !ReferenceEquals(node.Scene, message.Scene)))
                {
                    this.items = [];
                }

                // A fresh document session hydrates the authoritative typed context for that
                // document: a folder selection reopens as the grouping summary, not scene properties.
                this.selectionContext = this.sceneSelectionService.GetContext(message.Metadata.DocumentId);
                this.items = this.HasSelectionSummary ? [] : this.sceneSelectionService.GetSelectedNodes(message.Metadata.DocumentId, message.Scene).ToList();

                this.activeScene = message.Scene;
                this.RefreshPendingLiveSyncState();
                this.environmentEditor.SetScene(this.HasEnvironmentView ? message.Scene : null);
                this.NotifySelectionRoutingChanged();
                this.UpdateItemsCollection(this.items);
                this.SubscribeToComponentCollections();
                this.ApplyPendingEnvironmentFocus();
            }));

        this.RegisterSelectionMessages(hosting);
    }

    private void RegisterSelectionMessages(HostingContext hosting)
    {
        this.messenger.Register<SceneNodeSelectionChangedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() =>
            {
                if (this.isDisposed || (message.DocumentId is { } documentId
                    && (this.documentService.GetActiveDocumentId(this.windowId) != documentId
                        || !ReferenceEquals(this.sceneSelectionService.GetContext(documentId), message.SelectionContext))))
                {
                    return;
                }

                this.selectionContext = message.SelectionContext;

                // Folder and mixed batches show the grouping summary: their node subset must
                // not populate component editors as if the whole selection were nodes.
                this.items = this.HasSelectionSummary ? [] : message.SelectedEntities;
                this.activeScene = this.items.FirstOrDefault()?.Scene ?? this.activeScene;
                this.RefreshPendingLiveSyncState();
                this.environmentEditor.SetScene(this.HasEnvironmentView ? this.activeScene : null);
                this.NotifySelectionRoutingChanged();
                this.LogSelectionChanged(this.items.Count);
                this.UpdateItemsCollection(this.items);
                this.SubscribeToComponentCollections();
            }));

        this.messenger.Register<SceneLoadedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() =>
            {
                if (this.isDisposed || this.documentService.GetActiveDocumentId(this.windowId) != message.Scene.Id)
                {
                    return;
                }

                // SceneLoaded also arrives after a native synchronization of the same scene. A
                // sync refresh must never discard the author's folder/mixed classification; only
                // a genuinely different scene re-hydrates the typed context.
                if (!ReferenceEquals(message.Scene, this.activeScene))
                {
                    this.selectionContext = this.sceneSelectionService.GetContext(message.Scene.Id);
                    this.items = this.HasSelectionSummary ? [] : this.sceneSelectionService.GetSelectedNodes(message.Scene.Id, message.Scene).ToList();

                    this.activeScene = message.Scene;
                }

                this.RefreshPendingLiveSyncState();
                this.environmentEditor.SetScene(this.HasEnvironmentView ? this.activeScene : null);
                this.NotifySelectionRoutingChanged();
                this.OnPropertyChanged(nameof(this.HasInspectorContent));
                this.UpdateItemsCollection(this.items);
                this.SubscribeToComponentCollections();
                this.ApplyPendingEnvironmentFocus();
            }));
    }

    private void RegisterComponentMessages(HostingContext hosting)
    {
        // Listen for component add/remove requests coming from the details view (sent via global messenger)
        WeakReferenceMessenger.Default.Register<Messages.ComponentAddRequestedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() => this.OnComponentAddRequested(message)));

        WeakReferenceMessenger.Default.Register<Messages.ComponentRemoveRequestedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() => this.OnComponentRemoveRequested(message)));

        // Component collection changes are observed per-node via CollectionChanged subscriptions.
    }

    private void ApplyPendingEnvironmentFocus()
    {
        if (this.pendingEnvironmentFocus is not { } target || this.activeScene?.Id != target.sceneId)
        {
            return;
        }

        this.pendingEnvironmentFocus = null;
        this.items = [];
        this.environmentEditor.SetScene(this.activeScene);
        this.UpdateItemsCollection(this.items);
        this.environmentEditor.RequestFieldFocus(target.property);
    }

    private SceneDocumentCommandContext? CreateCommandContext()
    {
        var scene = this.items.FirstOrDefault()?.Scene ?? this.activeScene;
        return scene is null ? null : this.CreateCommandContext(scene);
    }

    private SceneDocumentCommandContext? CreateCommandContext(Scene scene)
    {
        if (scene is null)
        {
            return null;
        }

        var metadata = this.documentService.GetOpenDocuments(this.windowId)
            .OfType<SceneDocumentMetadata>()
            .FirstOrDefault(document => document.DocumentId == scene.Id);
        return metadata is null
            ? null
            : new SceneDocumentCommandContext(metadata.DocumentId, metadata, scene, UndoRedo.GetHistory(metadata.DocumentId));
    }

    private void OnPendingPropertySyncCountChanged(object? sender, PendingPropertySyncCountChangedEventArgs e)
    {
        if (this.activeScene?.Id != e.SceneId)
        {
            return;
        }

        void UpdateCount()
            => this.PendingLiveSyncEditCount = e.PendingCount;

        if (this.dispatcher is { HasThreadAccess: false })
        {
            _ = this.dispatcher.TryEnqueue(UpdateCount);
            return;
        }

        UpdateCount();
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
        => this.PendingLiveSyncEditCount = this.activeScene is null
            ? 0
            : this.sceneEngineSync.GetPendingPropertySyncCount(this.activeScene.Id);

    private void OnComponentAddRequested(Messages.ComponentAddRequestedMessage message)
    {
        if (message is null || message.Node is null || message.Component is null)
        {
            return;
        }

        _ = this.AddComponentRequestedAsync(message);
    }

    private void OnComponentRemoveRequested(Messages.ComponentRemoveRequestedMessage message)
    {
        if (message is null || message.Node is null || message.Component is null)
        {
            return;
        }

        _ = this.RemoveComponentRequestedAsync(message);
    }

    private async Task AddComponentRequestedAsync(Messages.ComponentAddRequestedMessage message)
    {
        if (this.CreateCommandContext(message.Node.Scene) is not { } context)
        {
            return;
        }

        _ = await this.commandService.AddComponentAsync(context, message.Node.Id, message.Component.GetType()).ConfigureAwait(true);
    }

    private async Task RemoveComponentRequestedAsync(Messages.ComponentRemoveRequestedMessage message)
    {
        if (this.CreateCommandContext(message.Node.Scene) is not { } context)
        {
            return;
        }

        _ = await this.commandService.RemoveComponentAsync(context, message.Node.Id, message.Component.Id).ConfigureAwait(true);
    }

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
