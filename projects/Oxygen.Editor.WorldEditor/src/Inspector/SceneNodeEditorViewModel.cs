// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Assets.Catalog;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
///     ViewModel for editing properties of selected SceneNode entities in the World Editor.
/// </summary>
public sealed partial class SceneNodeEditorViewModel : MultiSelectionDetails<SceneNode>, IDisposable
{
    // Map component type -> factory that creates an editor instance for a SceneNode.
    // Factories accept an IMessenger so editors can be created with the proper messenger instance
    // (we create per-SceneNodeEditorViewModel instances and cache them).
    private static readonly IDictionary<Type, Func<IMessenger?, IPropertyEditor<SceneNode>>> AllPropertyEditorFactories =
        new Dictionary<Type, Func<IMessenger?, IPropertyEditor<SceneNode>>>
        {
            { typeof(TransformComponent), messenger => new TransformViewModel(loggerFactory: null, messenger) },
        };

    private readonly ILogger logger;

    private readonly Dictionary<Type, IPropertyEditor<SceneNode>> editorInstances = [];
    private readonly IMessenger messenger;
    private readonly ISceneEngineSync sceneEngineSync;
    private readonly IAssetCatalog? assetCatalog;
    private readonly DispatcherQueue? dispatcher;
    private readonly Dictionary<INotifyCollectionChanged, SceneNode> componentNotifiers = [];

    private bool isDisposed;
    private ICollection<SceneNode> items;

    internal HistoryKeeper History
    {
        get
        {
            if (this.items.Count > 0)
            {
                return UndoRedo.Default[this.items.First().Scene.Id];
            }

            return UndoRedo.Default[this];
        }
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeEditorViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The hosting context for WinUI dispatching.</param>
    /// <param name="vmToViewConverter">The converter for resolving views from viewmodels.</param>
    /// <param name="messenger">The messenger for MVVM messaging.</param>
    /// <param name="sceneEngineSync">The scene engine synchronization service.</param>
    /// <param name="assetCatalog">The asset catalog for Content browser integration.</param>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the
    ///     recognition process. If <see langword="null" />, logging is disabled.
    /// </param>
    public SceneNodeEditorViewModel(HostingContext hosting, ViewModelToView vmToViewConverter, IMessenger messenger, ISceneEngineSync sceneEngineSync, IAssetCatalog assetCatalog, ILoggerFactory? loggerFactory = null)
        : base(loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<SceneNodeEditorViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<SceneNodeEditorViewModel>();
        this.LoggerFactory = loggerFactory;

        this.messenger = messenger;
        this.VmToViewConverter = vmToViewConverter;
        this.sceneEngineSync = sceneEngineSync;
        this.assetCatalog = assetCatalog;

        // Register GeometryComponent factory with access to AssetCatalog
        AllPropertyEditorFactories[typeof(GeometryComponent)] = messengerParam => new GeometryViewModel(hosting, assetCatalog, messengerParam!);

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

        // Component collection changes are observed per-node via CollectionChanged subscriptions

        // Handle transform applied messages from property editors: create undo entries and sync engine
        this.messenger.Register<SceneNodeTransformAppliedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() => this.OnTransformApplied(message)));

        // Handle geometry applied messages from property editors: create undo entries and sync engine
        this.messenger.Register<SceneNodeGeometryAppliedMessage>(this, (_, message) =>
            _ = hosting.Dispatcher.DispatchAsync(() => this.OnGeometryApplied(message)));
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

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.messenger.UnregisterAll(this);
        this.UnsubscribeAllComponentCollections();
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
        var keysToCheck = new HashSet<Type>(AllPropertyEditorFactories.Keys);
        Debug.WriteLine($"[SceneNodeEditorViewModel] AllPropertyEditorFactories has {AllPropertyEditorFactories.Count} factories: {string.Join(", ", AllPropertyEditorFactories.Keys.Select(k => k.Name))}");

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
        foreach (var kvp in AllPropertyEditorFactories)
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

        var before = AllPropertyEditorFactories.Count;
        var after = filteredEditors.Count;
        this.LogFiltered(before, after);

        return filteredEditors.Values;
    }

    private void OnTransformApplied(SceneNodeTransformAppliedMessage message)
    {
        if (message is null || message.Nodes.Count == 0)
        {
            return;
        }

        // Batch changes together
        var label = $"Transform edit ({message.Property})";
        this.History.BeginChangeSet(label);

        try
        {
            for (var i = 0; i < message.Nodes.Count; i++)
            {
                var node = message.Nodes[i];
                var oldSnap = message.OldValues[i];
                var newSnap = message.NewValues[i];

                var op = new TransformOperation(node, oldSnap, newSnap);
                this.History.AddChange($"Restore Transform ({node.Name})", this.ApplyRestoreTransform, op);

                // Immediate engine sync for the node (we already applied the new state in the editor)
                _ = this.sceneEngineSync.UpdateNodeTransformAsync(node);
            }
        }
        finally
        {
            this.History.EndChangeSet();
        }
    }

    private void OnGeometryApplied(SceneNodeGeometryAppliedMessage message)
    {
        if (message is null || message.Nodes.Count == 0)
        {
            return;
        }

        var label = $"Geometry edit ({message.Property})";
        this.History.BeginChangeSet(label);

        try
        {
            for (var i = 0; i < message.Nodes.Count; i++)
            {
                var node = message.Nodes[i];
                var oldSnap = message.OldValues[i];
                var newSnap = message.NewValues[i];

                var op = new GeometryOperation(node, oldSnap, newSnap);
                this.History.AddChange($"Restore Geometry ({node.Name})", this.ApplyRestoreGeometry, op);

                // Immediate engine sync for the node (we already applied the new state in the editor)
                var geo = node.Components.OfType<GeometryComponent>().FirstOrDefault();
                if (geo is not null)
                {
                    _ = this.sceneEngineSync.AttachGeometryAsync(node, geo);
                }
            }
        }
        finally
        {
            this.History.EndChangeSet();
        }
    }

    private void OnComponentAddRequested(Messages.ComponentAddRequestedMessage message)
    {
        if (message is null || message.Node is null || message.Component is null)
        {
            return;
        }

        var op = new ComponentOperation(message.Node, message.Component);
        this.ApplyAddComponent(op);
    }

    private void OnComponentRemoveRequested(Messages.ComponentRemoveRequestedMessage message)
    {
        if (message is null || message.Node is null || message.Component is null)
        {
            return;
        }

        var op = new ComponentOperation(message.Node, message.Component);
        this.ApplyRemoveComponent(op);
    }

    private void ApplyAddComponent(ComponentOperation? op)
    {
        Debug.Assert(op is not null, "ComponentOperation is null");

        _ = this.dispatcher?.DispatchAsync(() =>
        {
            try
            {
                var nodeComponentsBefore = op.Node.Components.Count;
                this.LogApplyAddComponent(op.Node.Name, op.Component.GetType().Name, op.Component.Name, nodeComponentsBefore);

                var added = op.Node.AddComponent(op.Component);

                if (added)
                {
                    // Record undo that removes this component
                    this.History.AddChange($"Remove Component ({op.Component.Name})", this.ApplyRemoveComponent, op);
                }

                // Notify result
                WeakReferenceMessenger.Default.Send(new Messages.ComponentAddedMessage(op.Node, op.Component, added));
            }
            catch (Exception ex)
            {
                this.LogUnexpectedException(nameof(this.ApplyAddComponent), ex);
                throw;
            }
        });
    }

    private void ApplyRemoveComponent(ComponentOperation? op)
    {
        Debug.Assert(op is not null, "ComponentOperation is null");

        _ = this.dispatcher?.DispatchAsync(() =>
        {
            try
            {
                var nodeComponentsBefore = op.Node.Components.Count;
                this.LogApplyDeleteComponent(op.Node.Name, op.Component.GetType().Name, op.Component.Name, nodeComponentsBefore, selectedComponentName: null);

                var removed = op.Node.RemoveComponent(op.Component);
                if (!removed)
                {
                    WeakReferenceMessenger.Default.Send(new Messages.ComponentRemovedMessage(op.Node, op.Component, removed: false));
                    return;
                }

                // Add undo to restore component
                this.History.AddChange($"Restore Component ({op.Component.Name})", this.ApplyAddComponent, op);

                // Engine sync: detach component-specific resources if necessary
                if (op.Component is GeometryComponent)
                {
                    _ = this.sceneEngineSync.DetachGeometryAsync(op.Node.Id);
                }

                WeakReferenceMessenger.Default.Send(new Messages.ComponentRemovedMessage(op.Node, op.Component, removed: true));
            }
            catch (Exception ex)
            {
                this.LogUnexpectedException(nameof(this.ApplyRemoveComponent), ex);
                throw;
            }
        });
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

    private void ApplyRestoreGeometry(GeometryOperation? op)
    {
        Debug.Assert(op is not null, "GeometryOperation is null");

        var geo = op.Node.Components.OfType<GeometryComponent>().FirstOrDefault();
        _ = geo?.Geometry = op.Old.UriString is not null ? new(op.Old.UriString) : null;

        if (geo is not null)
        {
            _ = this.sceneEngineSync.AttachGeometryAsync(op.Node, geo);
        }

        this.History.AddChange($"Reapply Geometry ({op.Node.Name})", this.ApplyReapplyGeometry, op);
    }

    private void ApplyReapplyGeometry(GeometryOperation? op)
    {
        Debug.Assert(op is not null, "GeometryOperation is null");

        var geo = op.Node.Components.OfType<GeometryComponent>().FirstOrDefault();
        _ = geo?.Geometry = op.New.UriString is not null ? new(op.New.UriString) : null;

        if (geo is not null)
        {
            _ = this.sceneEngineSync.AttachGeometryAsync(op.Node, geo);
        }

        this.History.AddChange($"Restore Geometry ({op.Node.Name})", this.ApplyRestoreGeometry, op);
    }

    private void ApplyRestoreTransform(TransformOperation? op)
    {
        Debug.Assert(op is not null, "GeometryOperation is null");

        var tr = op.Node.Components.OfType<TransformComponent>().FirstOrDefault();
        if (tr is not null)
        {
            tr.LocalPosition = op.Old.Position;
            tr.LocalRotation = op.Old.Rotation;
            tr.LocalScale = op.Old.Scale;
        }

        _ = this.sceneEngineSync.UpdateNodeTransformAsync(op.Node);
        this.History.AddChange($"Reapply Transform ({op.Node.Name})", this.ApplyReapplyTransform, op);
    }

    private void ApplyReapplyTransform(TransformOperation? op)
    {
        Debug.Assert(op is not null, "GeometryOperation is null");

        var tr = op.Node.Components.OfType<TransformComponent>().FirstOrDefault();
        if (tr is not null)
        {
            tr.LocalPosition = op.New.Position;
            tr.LocalRotation = op.New.Rotation;
            tr.LocalScale = op.New.Scale;
        }

        _ = this.sceneEngineSync.UpdateNodeTransformAsync(op.Node);
        this.History.AddChange($"Restore Transform ({op.Node.Name})", this.ApplyRestoreTransform, op);
    }

    private sealed record TransformOperation(SceneNode Node, TransformSnapshot Old, TransformSnapshot New);

    private sealed record GeometryOperation(SceneNode Node, GeometrySnapshot Old, GeometrySnapshot New);

    private sealed record ComponentOperation(SceneNode Node, GameComponent Component);
}
