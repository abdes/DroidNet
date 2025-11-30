// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Messages;
using Oxygen.Editor.WorldEditor.Services;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

/// <summary>
///     ViewModel for editing properties of selected SceneNode entities in the World Editor.
/// </summary>
public sealed partial class SceneNodeEditorViewModel : MultiSelectionDetails<SceneNode>, IDisposable
{
    // Map component type -> factory that creates an editor instance for a SceneNode.
    // Factories accept an IMessenger so editors can be created with the proper messenger instance
    // (we create per-SceneNodeEditorViewModel instances and cache them).
    private static readonly IDictionary<Type, Func<IMessenger?, IPropertyEditor<SceneNode>>> AllPropertyEditorFactories =
        new Dictionary<Type, Func<IMessenger?, IPropertyEditor<SceneNode>>> { { typeof(Transform), messenger => new TransformViewModel(loggerFactory: null, messenger) } };

    private readonly ILogger logger;

    private readonly IMessenger messenger;
    private readonly ISceneEngineSync sceneEngineSync;
    private bool isDisposed;

    private ICollection<SceneNode> items;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeEditorViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The hosting context for WinUI dispatching.</param>
    /// <param name="vmToViewConverter">The converter for resolving views from viewmodels.</param>
    /// <param name="messenger">The messenger for MVVM messaging.</param>
    /// <param name="loggerFactory">Optional logger factory for diagnostics.</param>
    public SceneNodeEditorViewModel(HostingContext hosting, ViewModelToView vmToViewConverter, IMessenger messenger, ISceneEngineSync sceneEngineSync, ILoggerFactory? loggerFactory = null)
        : base(loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<SceneNodeEditorViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<SceneNodeEditorViewModel>();

        this.messenger = messenger;
        this.VmToViewConverter = vmToViewConverter;
        this.sceneEngineSync = sceneEngineSync;

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
            this.messenger.Register<Messages.SceneNodeTransformAppliedMessage>(this, (_, message) =>
                hosting.Dispatcher.TryEnqueue(() => this.OnTransformApplied(message)));
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

    /// <inheritdoc />
    private readonly Dictionary<Type, IPropertyEditor<SceneNode>> editorInstances = new();

    protected override ICollection<IPropertyEditor<SceneNode>> FilterPropertyEditors()
    {
        var filteredEditors = new Dictionary<Type, IPropertyEditor<SceneNode>>();
        var keysToCheck = new HashSet<Type>(AllPropertyEditorFactories.Keys);

        foreach (var entity in this.items)
        {
            // Filter out keys for which the entity does not have a component
            foreach (var key in keysToCheck.ToList()
                         .Where(key => entity.Components.All(component => component.GetType() != key)))
            {
                _ = filteredEditors.Remove(key);
                _ = keysToCheck.Remove(key);
            }
        }

        // Ensure editor instances exist for all filtered factories and return instances
        foreach (var kvp in AllPropertyEditorFactories)
        {
            if (!keysToCheck.Contains(kvp.Key))
            {
                continue; // not applicable for current selection
            }

            // Create or reuse editor instance for this SceneNodeEditorViewModel
            if (!this.editorInstances.TryGetValue(kvp.Key, out var inst))
            {
                inst = kvp.Value(this.messenger);
                this.editorInstances[kvp.Key] = inst;
            }

            filteredEditors[kvp.Key] = inst;
        }

        var before = AllPropertyEditorFactories.Count;
        var after = filteredEditors.Count;
        this.LogFiltered(before, after);

        return filteredEditors.Values;
    }
    private void OnTransformApplied(Messages.SceneNodeTransformAppliedMessage message)
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

                // Add an undo action that restores the old snapshot and pushes a redo action
                UndoRedo.Default[this].AddChange(
                    $"Restore Transform ({node.Name})",
                    () =>
                    {
                        var tr = node.Components.OfType<Transform>().FirstOrDefault();
                        if (tr is not null)
                        {
                            tr.LocalPosition = oldSnap.Position;
                            tr.LocalRotation = oldSnap.Rotation;
                            tr.LocalScale = oldSnap.Scale;
                        }

                        // Add redo so that redoing will reapply the new snapshot
                        UndoRedo.Default[this].AddChange(
                            $"Reapply Transform ({node.Name})",
                            () =>
                            {
                                var tr2 = node.Components.OfType<Transform>().FirstOrDefault();
                                if (tr2 is not null)
                                {
                                    tr2.LocalPosition = newSnap.Position;
                                    tr2.LocalRotation = newSnap.Rotation;
                                    tr2.LocalScale = newSnap.Scale;
                                }
                            });
                    });

                // Immediate engine sync for the node (we already applied the new state in the editor)
                _ = this.sceneEngineSync.UpdateNodeTransformAsync(node);
            }
        }
        finally
        {
            UndoRedo.Default[this].EndChangeSet();
        }
    }
}
