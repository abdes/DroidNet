// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
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
using Oxygen.Assets.Catalog;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
///     ViewModel for editing properties of selected SceneNode entities in the World Editor.
/// </summary>
public sealed partial class SceneNodeEditorViewModel : MultiSelectionDetails<SceneNode>, IDisposable
{
    private readonly ILogger logger;

    private readonly Dictionary<Type, IPropertyEditor<SceneNode>> editorInstances = [];
    private readonly IDictionary<Type, Func<IMessenger?, IPropertyEditor<SceneNode>>> propertyEditorFactories;
    private readonly IMessenger messenger;
    private readonly ISceneDocumentCommandService commandService;
    private readonly IDocumentService documentService;
    private readonly WindowId windowId;
    private readonly DispatcherQueue? dispatcher;
    private readonly Dictionary<INotifyCollectionChanged, SceneNode> componentNotifiers = [];
    private readonly Dictionary<GameComponent, SceneNode> componentPropertyNotifiers = [];

    private bool isDisposed;
    private ICollection<SceneNode> items;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeEditorViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The hosting context for WinUI dispatching.</param>
    /// <param name="vmToViewConverter">The converter for resolving views from viewmodels.</param>
    /// <param name="messenger">The messenger for MVVM messaging.</param>
    /// <param name="assetCatalog">The asset catalog for Content browser integration.</param>
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
        IAssetCatalog assetCatalog,
        ILoggerFactory? loggerFactory = null)
        : base(loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<SceneNodeEditorViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<SceneNodeEditorViewModel>();
        this.LoggerFactory = loggerFactory;

        this.messenger = messenger;
        this.commandService = commandService;
        this.documentService = documentService;
        this.windowId = windowId;
        this.VmToViewConverter = vmToViewConverter;

        this.propertyEditorFactories = new Dictionary<Type, Func<IMessenger?, IPropertyEditor<SceneNode>>>
        {
            [typeof(TransformComponent)] = _ => new TransformViewModel(
                loggerFactory: null,
                commandService: commandService,
                commandContextProvider: this.CreateCommandContext),
            [typeof(GeometryComponent)] = _ => new GeometryViewModel(
                hosting,
                assetCatalog,
                commandService,
                this.CreateCommandContext),
        };

        this.items = this.messenger.Send(new SceneNodeSelectionRequestMessage()).SelectedEntities;
        this.UpdateItemsCollection(this.items);
        this.SubscribeToComponentCollections();
        this.LogConstructed(this.items.Count);
        this.dispatcher = hosting.Dispatcher;

        this.messenger.Register<SceneNodeSelectionChangedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() =>
            {
                this.items = message.SelectedEntities;
                this.LogSelectionChanged(this.items.Count);
                this.UpdateItemsCollection(this.items);
                this.SubscribeToComponentCollections();
            }));

        // Listen for component add/remove requests coming from the details view (sent via global messenger)
        WeakReferenceMessenger.Default.Register<Messages.ComponentAddRequestedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() => this.OnComponentAddRequested(message)));

        WeakReferenceMessenger.Default.Register<Messages.ComponentRemoveRequestedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() => this.OnComponentRemoveRequested(message)));
        // Component collection changes are observed per-node via CollectionChanged subscriptions.
    }

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

        this.messenger.UnregisterAll(this);
        this.UnsubscribeAllComponentCollections();
        foreach (var editor in this.editorInstances.Values.OfType<IDisposable>())
        {
            editor.Dispose();
        }

        this.editorInstances.Clear();
        this.LogDisposed();

        this.isDisposed = true;
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
    }

    /// <inheritdoc/>
    protected override ICollection<IPropertyEditor<SceneNode>> FilterPropertyEditors()
    {
        Debug.WriteLine($"[SceneNodeEditorViewModel] FilterPropertyEditors called. Items count: {this.items.Count}");
        var filteredEditors = new Dictionary<Type, IPropertyEditor<SceneNode>>();
        var keysToCheck = new HashSet<Type>(this.propertyEditorFactories.Keys);
        Debug.WriteLine($"[SceneNodeEditorViewModel] propertyEditorFactories has {this.propertyEditorFactories.Count} factories: {string.Join(", ", this.propertyEditorFactories.Keys.Select(k => k.Name))}");

        foreach (var entity in this.items)
        {
            Debug.WriteLine($"[SceneNodeEditorViewModel] Checking entity: {entity.Name}, Components: {string.Join(", ", entity.Components.Select(c => c.GetType().Name))}");

            // Filter out keys for which the entity does not have a component
            foreach (var key in keysToCheck.ToList()
                         .Where(key => entity.Components.All(component => component.GetType() != key)))
            {
                Debug.WriteLine($"[SceneNodeEditorViewModel] Removing key {key.Name} (component not found on entity)");
                _ = filteredEditors.Remove(key);
                _ = keysToCheck.Remove(key);
            }
        }

        // Ensure editor instances exist for all filtered factories and return instances
        Debug.WriteLine($"[SceneNodeEditorViewModel] Keys to check after filtering: {string.Join(", ", keysToCheck.Select(k => k.Name))}");
        foreach (var kvp in this.propertyEditorFactories)
        {
            if (!keysToCheck.Contains(kvp.Key))
            {
                continue; // not applicable for current selection
            }

            // Create or reuse editor instance for this SceneNodeEditorViewModel
            if (!this.editorInstances.TryGetValue(kvp.Key, out var inst))
            {
                Debug.WriteLine($"[SceneNodeEditorViewModel] Creating NEW editor instance for {kvp.Key.Name}");
                inst = kvp.Value(this.messenger);
                this.editorInstances[kvp.Key] = inst;
            }
            else
            {
                Debug.WriteLine($"[SceneNodeEditorViewModel] Reusing existing editor instance for {kvp.Key.Name}");
            }

            filteredEditors[kvp.Key] = inst;
        }

        var before = this.propertyEditorFactories.Count;
        var after = filteredEditors.Count;
        this.LogFiltered(before, after);

        return filteredEditors.Values;
    }

    private SceneDocumentCommandContext? CreateCommandContext()
    {
        var scene = this.items.FirstOrDefault()?.Scene;
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
            : new SceneDocumentCommandContext(metadata.DocumentId, metadata, scene, UndoRedo.Default[metadata.DocumentId]);
    }

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
    {
        // Unsubscribe previous
        this.UnsubscribeAllComponentCollections();

        foreach (var node in this.items)
        {
            if (node.Components is INotifyCollectionChanged notifier)
            {
                notifier.CollectionChanged += this.OnNodeComponentsChanged;
                this.componentNotifiers[notifier] = node;
            }

            this.SubscribeToComponentProperties(node);
        }
    }

    private void UnsubscribeAllComponentCollections()
    {
        foreach (var kvp in this.componentNotifiers.ToList())
        {
            try
            {
                kvp.Key.CollectionChanged -= this.OnNodeComponentsChanged;
            }
            catch
            {
                // ignore
            }
        }

        this.componentNotifiers.Clear();

        foreach (var component in this.componentPropertyNotifiers.Keys.ToList())
        {
            try
            {
                component.PropertyChanged -= this.OnSelectedComponentPropertyChanged;
            }
            catch
            {
                // ignore
            }
        }

        this.componentPropertyNotifiers.Clear();
    }

    private void OnNodeComponentsChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (sender is not INotifyCollectionChanged notifier)
        {
            return;
        }

        if (!this.componentNotifiers.TryGetValue(notifier, out var node))
        {
            return;
        }

        _ = this.dispatcher?.DispatchAsync(() => // Replaced SafeEnqueue with DispatchAsync
        {
            this.LogComponentsUpdateEnqueued("ComponentCollectionChanged", node.Name, e.NewItems?.OfType<object>().FirstOrDefault()?.GetType().Name ?? string.Empty);

            // Rebuild property editors for current selection
            this.UpdateItemsCollection(this.items);
        });
    }

    private void SubscribeToComponentProperties(SceneNode node)
    {
        foreach (var component in node.Components)
        {
            if (this.componentPropertyNotifiers.ContainsKey(component))
            {
                continue;
            }

            component.PropertyChanged += this.OnSelectedComponentPropertyChanged;
            this.componentPropertyNotifiers[component] = node;
        }
    }

    private void OnSelectedComponentPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        _ = sender;
        _ = e;
        _ = this.dispatcher?.DispatchAsync(this.RefreshPropertyEditorValues);
    }

}
