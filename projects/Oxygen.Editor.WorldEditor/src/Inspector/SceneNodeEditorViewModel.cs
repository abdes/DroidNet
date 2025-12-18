// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Messages;
using Oxygen.Editor.WorldEditor.Services;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
///     ViewModel for editing properties of selected SceneNode entities in the World Editor.
/// </summary>
public sealed partial class SceneNodeEditorViewModel : MultiSelectionDetails<SceneNode>, IDisposable
{
    private sealed record TransformOperation(SceneNode Node, TransformSnapshot Old, TransformSnapshot New);
    private sealed record GeometryOperation(SceneNode Node, GeometrySnapshot Old, GeometrySnapshot New);

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
    private readonly ContentBrowser.AssetsIndexingService? assetsIndexingService;
    private bool isDisposed;

    private ICollection<SceneNode> items;

    /// <summary>
    /// Gets a value indicating whether exactly one item is selected.
    /// </summary>
    public bool IsSingleItemSelected => this.items.Count == 1;

    /// <summary>
    /// Gets the single selected node, or <see langword="null"/> when the selection is empty or contains multiple nodes.
    /// Intended for binding into <see cref="SceneNodeDetailsView"/>.
    /// </summary>
    public SceneNode? SelectedNode => this.items.Count == 1 ? this.items.First() : null;

    public ILoggerFactory? LoggerFactory { get; private set; }

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

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeEditorViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The hosting context for WinUI dispatching.</param>
    /// <param name="vmToViewConverter">The converter for resolving views from viewmodels.</param>
    /// <param name="messenger">The messenger for MVVM messaging.</param>
    /// <param name="sceneEngineSync">The scene engine synchronization service.</param>
    /// <param name="assetsIndexingService">The assets indexing service for Content browser integration.</param>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the
    ///     recognition process. If <see langword="null" />, logging is disabled.
    /// </param>
    public SceneNodeEditorViewModel(HostingContext hosting, ViewModelToView vmToViewConverter, IMessenger messenger, ISceneEngineSync sceneEngineSync, ContentBrowser.IAssetIndexingService assetsIndexingService, ILoggerFactory? loggerFactory = null)
        : base(loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<SceneNodeEditorViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<SceneNodeEditorViewModel>();
        this.LoggerFactory = loggerFactory;

        this.messenger = messenger;
        this.VmToViewConverter = vmToViewConverter;
        this.sceneEngineSync = sceneEngineSync;
        this.assetsIndexingService = assetsIndexingService as ContentBrowser.AssetsIndexingService;

        Debug.WriteLine($"[SceneNodeEditorViewModel] Constructor - AssetsIndexingService is {(this.assetsIndexingService != null ? "available" : "NULL")}");

        // Register GeometryComponent factory with access to AssetsIndexingService
        AllPropertyEditorFactories[typeof(GeometryComponent)] = messengerParam =>
        {
            Debug.WriteLine($"[SceneNodeEditorViewModel] Creating GeometryViewModel with AssetsIndexingService: {(assetsIndexingService != null ? "available" : "NULL")}");
            return new GeometryViewModel(assetsIndexingService, messengerParam);
        };

        this.items = this.messenger.Send(new SceneNodeSelectionRequestMessage()).SelectedEntities;
        this.UpdateItemsCollection(this.items);
        this.LogConstructed(this.items.Count);

        this.messenger.Register<SceneNodeSelectionChangedMessage>(this, (_, message) =>
            hosting.Dispatcher.TryEnqueue(() =>
            {
                this.items = message.SelectedEntities;
                this.LogSelectionChanged(this.items.Count);
                this.UpdateItemsCollection(this.items);
            }));

        // Handle transform applied messages from property editors: create undo entries and sync engine
        this.messenger.Register<SceneNodeTransformAppliedMessage>(this, (_, message) =>
            hosting.Dispatcher.TryEnqueue(() => this.OnTransformApplied(message)));

        // Handle geometry applied messages from property editors: create undo entries and sync engine
        this.messenger.Register<SceneNodeGeometryAppliedMessage>(this, (_, message) =>
            hosting.Dispatcher.TryEnqueue(() => this.OnGeometryApplied(message)));
    }

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
        this.LogDisposed();

        this.isDisposed = true;
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
        UndoRedo.Default[this].BeginChangeSet(label);

        try
        {
            for (var i = 0; i < message.Nodes.Count; i++)
            {
                var node = message.Nodes[i];
                var oldSnap = message.OldValues[i];
                var newSnap = message.NewValues[i];

                var op = new TransformOperation(node, oldSnap, newSnap);
                UndoRedo.Default[this].AddChange($"Restore Transform ({node.Name})", this.ApplyRestoreTransform, op);

                // Immediate engine sync for the node (we already applied the new state in the editor)
                _ = this.sceneEngineSync.UpdateNodeTransformAsync(node);
            }
        }
        finally
        {
            UndoRedo.Default[this].EndChangeSet();
        }
    }

    private void OnGeometryApplied(SceneNodeGeometryAppliedMessage message)
    {
        if (message is null || message.Nodes.Count == 0)
        {
            return;
        }

        var label = $"Geometry edit ({message.Property})";
        UndoRedo.Default[this].BeginChangeSet(label);

        try
        {
            for (var i = 0; i < message.Nodes.Count; i++)
            {
                var node = message.Nodes[i];
                var oldSnap = message.OldValues[i];
                var newSnap = message.NewValues[i];

                var op = new GeometryOperation(node, oldSnap, newSnap);
                UndoRedo.Default[this].AddChange($"Restore Geometry ({node.Name})", this.ApplyRestoreGeometry, op);

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
            UndoRedo.Default[this].EndChangeSet();
        }
    }

    private void ApplyRestoreGeometry(GeometryOperation? op)
    {
        ArgumentNullException.ThrowIfNull(op);

        var geo = op.Node.Components.OfType<GeometryComponent>().FirstOrDefault();
        if (geo is not null)
        {
            geo.Geometry.Uri = op.Old.UriString is not null ? new Uri(op.Old.UriString) : null;
        }

        if (geo is not null)
        {
            _ = this.sceneEngineSync.AttachGeometryAsync(op.Node, geo);
        }

        UndoRedo.Default[this].AddChange($"Reapply Geometry ({op.Node.Name})", this.ApplyReapplyGeometry, op);
    }

    private void ApplyReapplyGeometry(GeometryOperation? op)
    {
        ArgumentNullException.ThrowIfNull(op);

        var geo = op.Node.Components.OfType<GeometryComponent>().FirstOrDefault();
        if (geo is not null)
        {
            geo.Geometry.Uri = op.New.UriString is not null ? new Uri(op.New.UriString) : null;
        }

        if (geo is not null)
        {
            _ = this.sceneEngineSync.AttachGeometryAsync(op.Node, geo);
        }

        UndoRedo.Default[this].AddChange($"Restore Geometry ({op.Node.Name})", this.ApplyRestoreGeometry, op);
    }

    private void ApplyRestoreTransform(TransformOperation? op)
    {
        ArgumentNullException.ThrowIfNull(op);

        var tr = op.Node.Components.OfType<TransformComponent>().FirstOrDefault();
        if (tr is not null)
        {
            tr.LocalPosition = op.Old.Position;
            tr.LocalRotation = op.Old.Rotation;
            tr.LocalScale = op.Old.Scale;
        }

        _ = this.sceneEngineSync.UpdateNodeTransformAsync(op.Node);
        UndoRedo.Default[this].AddChange($"Reapply Transform ({op.Node.Name})", this.ApplyReapplyTransform, op);
    }

    private void ApplyReapplyTransform(TransformOperation? op)
    {
        ArgumentNullException.ThrowIfNull(op);

        var tr = op.Node.Components.OfType<TransformComponent>().FirstOrDefault();
        if (tr is not null)
        {
            tr.LocalPosition = op.New.Position;
            tr.LocalRotation = op.New.Rotation;
            tr.LocalScale = op.New.Scale;
        }

        _ = this.sceneEngineSync.UpdateNodeTransformAsync(op.Node);
        UndoRedo.Default[this].AddChange($"Restore Transform ({op.Node.Name})", this.ApplyRestoreTransform, op);
    }
}
