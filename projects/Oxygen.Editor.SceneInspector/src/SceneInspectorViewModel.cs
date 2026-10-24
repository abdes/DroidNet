// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
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
using Oxygen.Editor.SceneInspector.Components;
using Oxygen.Editor.SceneInspector.Components.Scripting;
using Oxygen.Editor.SceneInspector.SceneSettings;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
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
/// The Properties panel of a scene: the sections that edit the selected nodes' components, or the scene's settings when
/// the scene itself is selected, each edit going through the scene document's authoring.
/// </summary>
[RetainedViewModel]
public sealed partial class SceneInspectorViewModel : ObservableObject, ISceneNodeEdits, IDisposable
{
    // The selection source of this inspector's "inspect this node" requests; the Scene Explorer reveals and focuses the row.
    private const string InspectorSelectionSource = "Inspector";

    private readonly ILogger logger;

    private readonly Dictionary<Type, ComponentPropertyEditor> editorInstances = [];
    private readonly Dictionary<Type, Func<ComponentPropertyEditor>> propertyEditorFactories;
    private readonly IAssetReveal assetReveal;
    private readonly InspectedSceneDocuments documents;
    private readonly IScenePreview scenePreview;
    private readonly SceneNodeInteraction? interaction;
    private readonly DispatcherQueue? dispatcher;
    private readonly InspectorSelectionObserver selectionObserver;
    private readonly SceneSettingsViewModel sceneSettings;
    private readonly ISceneSelectionService sceneSelectionService;

    // The nodes whose property changes refresh the panel's own properties.
    private readonly List<SceneNode> observedNodes = [];
    private (DocumentId document, string property)? pendingEnvironmentFocus;

    // The atmosphere source the inspector selected, whose directional light it shows once the selection arrives.
    private NodeId? pendingAtmosphereSource;

    private bool isDisposed;
    private ICollection<SceneNode> items;
    private Scene? activeScene;

    // The document that edits the active scene; selection and scene lifetime messages name it.
    private DocumentId? activeDocumentId;
    private int pendingLiveSyncEditCount;

    // The classified row-kind context of the latest selection: the node list alone cannot say
    // whether the author selected the root, folders, or a mixed batch.
    private SceneSelectionContext selectionContext = SceneSelectionContext.Empty;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneInspectorViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The hosting context for WinUI dispatching.</param>
    /// <param name="vmToViewConverter">The converter for resolving views from viewmodels.</param>
    /// <param name="assetReveal">What shows assets in the Content Explorer.</param>
    /// <param name="documentArea">The window's document area, whose active scene document the inspector shows.</param>
    /// <param name="assetProvider">The shared asset identity and availability feed.</param>
    /// <param name="scenePreview">The engine preview, which reports the fields still waiting for the engine.</param>
    /// <param name="selectionService">The authoritative document selection owner, used to hydrate the typed context at startup.</param>
    /// <param name="surfaces">The current native geometry surface inventories.</param>
    /// <param name="session">The project session the inspector belongs to.</param>
    /// <param name="inputSources">The project's input sources, which declare the scene's starting input contexts.</param>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the
    ///     recognition process. If <see langword="null" />, logging is disabled.
    /// </param>
    /// <param name="interaction">The shared editor lock owner.</param>
    /// <param name="scripts">The scripts, for script slot parameters and problems.</param>
    /// <param name="codeEditor">The code editor, for editing a slot's script.</param>
    /// <param name="scriptCreation">What creates a script for a slot, with New Script.</param>
    /// <param name="problems">What shows a script's problem in the Problems panel.</param>
    public SceneInspectorViewModel(
        HostingContext hosting,
        ViewModelToView vmToViewConverter,
        IAssetReveal assetReveal,
        IDocumentArea documentArea,
        IAssetFeed assetProvider,
        IScenePreview scenePreview,
        ISceneSelectionService selectionService,
        IGeometrySurfaceProvider surfaces,
        IProjectSession session,
        IInputSources inputSources,
        ILoggerFactory? loggerFactory = null,
        SceneNodeInteraction? interaction = null,
        IScripts? scripts = null,
        ICodeEditorService? codeEditor = null,
        IScriptCreation? scriptCreation = null,
        IProblemReveal? problems = null)
    {
        this.logger = loggerFactory?.CreateLogger<SceneInspectorViewModel>() ?? NullLogger<SceneInspectorViewModel>.Instance;
        this.LoggerFactory = loggerFactory;

        this.assetReveal = assetReveal;
        this.documents = new InspectedSceneDocuments(documentArea, this.ShowDocument, this.ClearClosedDocument);
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
            surfaces,
            session,
            loggerFactory,
            this.AuthoringOf,
            this.SelectNode,
            new InspectorScriptServices(scripts, codeEditor, scriptCreation, problems));
        this.sceneSettings = new SceneSettingsViewModel(
            this.AuthoringOf,
            assetProvider,
            this.InspectAtmosphereSource,
            hosting.DispatcherScheduler,
            this.ObserveSkyLightAsync,
            context => StartingContextRules.Describe(session.Context, context, inputSources.Find),
            asset => this.RevealAssetAsync(asset),
            session,
            inputSources,
            loggerFactory);

        // Startup hydrates the authoritative typed context with the nodes: a folder-only selection
        // must initialize as the grouping summary, not as "scene properties".
        var activeDocument = this.documents.ActiveSceneId;
        this.activeScene = this.documents.SceneOf(activeDocument);
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
        this.sceneSettings.SetScene(this.HasSceneSettings ? this.activeScene : null);
        this.ShowItems();
        this.SubscribeToComponentCollections();
        this.LogConstructed(this.items.Count);

        this.sceneSelectionService.SelectionChanged += this.OnSelectionServiceChanged;
    }

    /// <summary>Gets the sections shown for the selection: the scene's settings, or the selected nodes' components.</summary>
    public ObservableCollection<InspectorSection> PropertyEditors { get; } = [];

    /// <summary>Gets a value indicating whether any node is selected.</summary>
    public bool HasItems => this.items.Count > 0;

    /// <summary>Gets a value indicating whether several nodes are selected.</summary>
    public bool HasMultipleItems => this.items.Count > 1;

    /// <summary>Gets the number of selected nodes.</summary>
    public int ItemsCount => this.items.Count;

    /// <summary>Gets the scene-level editor hosted outside component-property scrolling.</summary>
    public SceneSettingsViewModel SceneSettingsEditor => this.sceneSettings;

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
                ? $"'{node.Metadata.Name}' is locked. It remains selectable for inspection, but its properties cannot be changed."
                : $"'{node.Metadata.Name}' is protected by locked parent '{lockOwner.Metadata.Name}'. Its properties cannot be changed.";
        }
    }

    /// <summary>Gets a value indicating whether the scene settings editor is the active surface.</summary>
    public bool HasSceneSettings => !this.HasItems
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
    /// nothing is logged.
    /// </summary>
    public ILoggerFactory? LoggerFactory { get; }

    /// <summary>
    ///     Gets a viewmodel to view converter provided by the local Ioc container, which can resolve
    ///     view from viewmodels registered locally. This converter must be used instead of the default
    ///     Application converter.
    /// </summary>
    public ViewModelToView VmToViewConverter { get; }

    // The scene the panel inspects: the selected nodes' scene, or the active scene document's.
    private Scene? InspectedScene => this.items.FirstOrDefault()?.Scene ?? this.activeScene;

    // The history of the inspected scene's document; none while no scene document is open.
    private HistoryKeeper? History => this.documents.DocumentOf(this.InspectedScene)?.History;

    /// <summary>Refreshes the visible sections' values from the selected nodes and the scene.</summary>
    public void RefreshPropertyEditorValues()
    {
        this.RefreshOwnProperties();
        this.UpdatePropertyEditorsValues();
    }

    /// <inheritdoc />
    public async Task AddComponentAsync(SceneNode node, string kind)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(kind);
        if (this.documents.AuthoringOf(node.Scene) is { } authoring)
        {
            _ = await authoring.ExecuteAsync(SceneComponents.Add(authoring, node.Id, kind)).ConfigureAwait(true);
        }
    }

    /// <inheritdoc />
    public async Task<bool> RenameAsync(SceneNode node, string name)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(name);
        return this.documents.AuthoringOf(node.Scene) is { } authoring
            && (await authoring.ExecuteAsync(new RenameNode(node.Id, name)).ConfigureAwait(true)).Succeeded;
    }

    /// <inheritdoc />
    public async Task RemoveComponentAsync(SceneNode node, GameComponent component)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(component);
        if (this.documents.AuthoringOf(node.Scene) is { } authoring)
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
        this.documents.Dispose();
        if (this.interaction is { } interactionService)
        {
            interactionService.StateChanged -= this.OnWorkspaceInteractionStateChanged;
        }

        this.ObserveNodes([]);
        this.selectionObserver.Dispose();
        this.StopObservingComponentFeedback();
        foreach (var editor in this.editorInstances.Values.OfType<IDisposable>())
        {
            editor.Dispose();
        }

        this.sceneSettings.Dispose();
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

    // Shows the current nodes: follows their property changes, and shows the sections that apply to them.
    private void ShowItems()
    {
        this.ObserveNodes(this.items);
        this.RefreshOwnProperties();
        this.RefreshPropertyEditors();
    }

    private void ObserveNodes(IEnumerable<SceneNode> nodes)
    {
        foreach (var node in this.observedNodes)
        {
            node.PropertyChanged -= this.OnNodePropertyChanged;
        }

        this.observedNodes.Clear();
        this.observedNodes.AddRange(nodes);
        foreach (var node in this.observedNodes)
        {
            node.PropertyChanged += this.OnNodePropertyChanged;
        }
    }

    private void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs args) => this.RefreshOwnProperties();

    private void RefreshOwnProperties()
    {
        try
        {
            this.OnPropertyChanged(nameof(this.ItemsCount));
            this.OnPropertyChanged(nameof(this.HasItems));
            this.OnPropertyChanged(nameof(this.HasMultipleItems));
            this.OnPropertyChanged(nameof(this.IsSingleItemSelected));
            this.OnPropertyChanged(nameof(this.SelectedNode));
            this.NotifyLockEligibilityChanged();
            this.OnPropertyChanged(nameof(this.HasInspectorContent));
            this.NotifySelectionRoutingChanged();
            this.RefreshPendingLiveSyncState();
        }
        catch (Exception ex)
        {
            this.LogUnexpectedException(nameof(this.RefreshOwnProperties), ex);
            throw;
        }
    }

    /// <summary>Refreshes section visibility and binds each visible section to the current selection.</summary>
    /// <param name="refreshValues">Whether the authoring selection or source values also need rebinding.</param>
    private void RefreshPropertyEditors(bool refreshValues = true)
    {
        var shown = this.FilterPropertyEditors();
        for (var index = this.PropertyEditors.Count - 1; index >= 0; index--)
        {
            var section = this.PropertyEditors[index];
            if (!shown.Contains(section))
            {
                this.PropertyEditors.RemoveAt(index);
                section.SetInputEnabled(enabled: false);
            }
        }

        // Preserve canonical section order when a filter restores existing sections.
        var targetIndex = 0;
        foreach (var section in shown)
        {
            var currentIndex = this.PropertyEditors.IndexOf(section);
            if (currentIndex < 0)
            {
                this.PropertyEditors.Insert(targetIndex, section);
            }
            else if (currentIndex != targetIndex)
            {
                this.PropertyEditors.Move(currentIndex, targetIndex);
            }

            ++targetIndex;
        }

        if (refreshValues)
        {
            this.UpdatePropertyEditorsValues();
        }
    }

    private List<InspectorSection> FilterPropertyEditors()
    {
        var result = new List<InspectorSection>();
        this.sceneSettings.SetScene(this.HasSceneSettings ? this.activeScene : null);
        if (this.HasSceneSettings)
        {
            result.Add(this.sceneSettings);
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

        // All mode stacks the sections in the component list's order.
        var shown = this.propertyEditorFactories
            .Where(factory => keysToCheck.Contains(factory.Key))
            .OrderBy(static factory => ComponentOrder(factory.Key))
            .ThenBy(static factory => ComponentLabel(factory.Key), StringComparer.Ordinal)
            .Select(this.GetOrCreatePropertyEditor)
            .ToList();
        this.LogFiltered(this.propertyEditorFactories.Count, shown.Count);
        result.AddRange(shown);
        return result;
    }

    private void UpdatePropertyEditorsValues()
    {
        var selection = this.items.ToArray();
        foreach (var editor in this.editorInstances.Values.ToArray())
        {
            editor.UpdateValues(selection);
            _ = this.boundEditors.Add(editor);
            editor.SetInputEnabled(this.IsEditorInputEnabled(editor));
        }

        this.sceneSettings.Refresh();
        this.sceneSettings.SetInputEnabled(this.PropertyEditors.Contains(this.sceneSettings));
    }

    /// <summary>Raises the change notifications for the selection-kind routing surface.</summary>
    private void NotifySelectionRoutingChanged()
    {
        this.OnPropertyChanged(nameof(this.HasSelectionSummary));
        this.OnPropertyChanged(nameof(this.HasSelectionNotice));
        this.OnPropertyChanged(nameof(this.HasSceneSettings));
        this.OnPropertyChanged(nameof(this.SelectionSummaryText));
        this.OnPropertyChanged(nameof(this.InspectorTitle));
    }

    private Task<SkyLightObservation?> ObserveSkyLightAsync(Scene scene, CancellationToken cancellationToken)
        => this.documents.DocumentOf(scene) is { } document
            ? this.scenePreview.ObserveSkyLightAsync(document.Id, cancellationToken)
            : Task.FromResult<SkyLightObservation?>(null);

    private bool IsEditorInputEnabled(ComponentPropertyEditor editor)
        => this.PropertyEditors.Contains(editor) && this.CanEditSelectedNodes;

    // Ctrl+B's navigation: the Content Explorer shows and selects the asset.
    private async Task RevealAssetAsync(AssetPath asset)
    {
        _ = await this.assetReveal.RevealAsync([asset]).ConfigureAwait(true);
    }

    // Selects a node of the inspected scene, such as the ancestor an inherited node flag comes from.
    private void SelectNode(NodeId nodeId)
    {
        if (this.documents.ActiveSceneId is not { } documentId
            || this.activeScene?.AllNodes.FirstOrDefault(candidate => candidate.Id == nodeId) is not { } node)
        {
            return;
        }

        this.sceneSelectionService.SetSelection(documentId, [node], InspectorSelectionSource);
    }

    private async Task InspectAtmosphereSource(NodeId nodeId)
    {
        var node = this.activeScene?.AllNodes.FirstOrDefault(candidate => candidate.Id == nodeId);
        if (node is null)
        {
            return;
        }

        if (this.documents.ActiveSceneId is not { } documentId)
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
        foreach (var entity in this.items)
        {
            keysToCheck.RemoveWhere(key => entity.Components.All(component => component.GetType() != key));
        }

        return keysToCheck;
    }

    private ComponentPropertyEditor GetOrCreatePropertyEditor(KeyValuePair<Type, Func<ComponentPropertyEditor>> factory)
    {
        if (this.editorInstances.TryGetValue(factory.Key, out var instance))
        {
            return instance;
        }

        instance = factory.Value();
        instance.SetInputEnabled(enabled: false);
        this.editorInstances[factory.Key] = instance;
        this.ObserveComponentFeedback(factory.Key, instance);
        return instance;
    }

    // A scene document that became active, or reloaded while active: the inspector shows its scene.
    private void ShowDocument(Document document)
    {
        if (this.isDisposed)
        {
            return;
        }

        var scene = SceneContent.Of(document).Scene;

        // A fresh document session hydrates the authoritative typed context for that
        // document: a folder selection reopens as the grouping summary, not scene properties.
        this.selectionContext = this.sceneSelectionService.GetContext(document.Id);
        this.items = this.HasSelectionSummary ? [] : this.sceneSelectionService.GetSelectedNodes(document.Id, scene).ToList();

        this.activeScene = scene;
        this.activeDocumentId = document.Id;
        this.RefreshPendingLiveSyncState();
        this.sceneSettings.SetScene(this.HasSceneSettings ? scene : null);
        this.NotifySelectionRoutingChanged();
        this.ShowItems();
        this.SubscribeToComponentCollections();
        this.ApplyPendingEnvironmentFocus();
    }

    // The watched scene document closed: the inspector has nothing to show for it.
    private void ClearClosedDocument(DocumentId documentId)
    {
        if (this.isDisposed)
        {
            return;
        }

        _ = documentId;
        this.items = [];
        this.SubscribeToComponentCollections();
        this.activeScene = null;
        this.activeDocumentId = null;
        this.selectionContext = this.sceneSelectionService.GetContext(DocumentId.Empty);
        this.RefreshPendingLiveSyncState();
        this.sceneSettings.SetScene(value: null);
        this.NotifySelectionRoutingChanged();
        this.ShowItems();
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
            || this.documents.ActiveSceneId != args.DocumentId
            || !ReferenceEquals(this.sceneSelectionService.GetContext(args.DocumentId), args.Context))
        {
            return;
        }

        this.selectionContext = args.Context;

        // Folder and mixed batches show the grouping summary: their node subset must
        // not populate component editors as if the whole selection were nodes.
        var scene = this.documents.SceneOf(args.DocumentId);
        this.items = this.HasSelectionSummary || scene is null
            ? []
            : this.sceneSelectionService.GetSelectedNodes(args.DocumentId, scene).ToList();
        if (this.items.FirstOrDefault()?.Scene is { } selectedScene)
        {
            this.activeScene = selectedScene;
            this.activeDocumentId = args.DocumentId;
        }

        this.RefreshPendingLiveSyncState();
        this.sceneSettings.SetScene(this.HasSceneSettings ? this.activeScene : null);
        this.NotifySelectionRoutingChanged();
        this.LogSelectionChanged(this.items.Count);
        this.ShowItems();
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
        this.sceneSettings.SetScene(this.activeScene);
        this.ShowItems();
        this.sceneSettings.RequestFieldFocus(target.property);
    }

    // The scene's properties show once the scene is the shown one; until then the request waits.
    private void OnPropertyFocusRequested(object? sender, ScenePropertyFocusEventArgs args)
    {
        this.pendingEnvironmentFocus = (args.DocumentId, args.PropertyPath);
        this.ApplyPendingEnvironmentFocus();
    }

    private SceneAuthoring? AuthoringOf() => this.documents.AuthoringOf(this.InspectedScene);

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
            this.ShowItems();
            this.SubscribeToComponentCollections();
        }
        else
        {
            this.RefreshPropertyEditorValues();
        }
    }
}
