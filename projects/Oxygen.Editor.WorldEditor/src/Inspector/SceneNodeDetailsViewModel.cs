// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Menus;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
/// ViewModel for <see cref="SceneNodeDetailsView"/>. This ViewModel assumes a single <see cref="SceneNode"/>.
/// </summary>
public sealed partial class SceneNodeDetailsViewModel : ObservableObject
{
    private sealed record ComponentOperation(SceneNode Node, GameComponent Component);

    private ILogger logger;
    private readonly DispatcherQueue? dispatcher;

    private ILoggerFactory? loggerFactory;

    private bool doNotPropagateName;

    private SceneNode? node;

    /// <summary>
    /// Gets or sets the object used as the undo/redo history root.
    /// When set (typically to the parent inspector ViewModel), changes are recorded into that history.
    /// </summary>
    public object? HistoryRoot { get; set; }

    /// <summary>
    /// Gets or sets the <see cref="ILoggerFactory"/> used by this view-model.
    /// Setting this updates the internal logger instance.
    /// </summary>
    public ILoggerFactory? LoggerFactory
    {
        get => this.loggerFactory;
        set
        {
            if (ReferenceEquals(this.loggerFactory, value))
            {
                return;
            }

            this.loggerFactory = value;
            this.logger = (value ?? NullLoggerFactory.Instance).CreateLogger<SceneNodeDetailsViewModel>();
        }
    }

    public SceneNodeDetailsViewModel(ILoggerFactory? loggerFactory = null)
    {
        this.dispatcher = DispatcherQueue.GetForCurrentThread();
        this.logger = NullLoggerFactory.Instance.CreateLogger<SceneNodeDetailsViewModel>();
        this.LoggerFactory = loggerFactory;
        this.LogConstructed(this.dispatcher is not null);
    }

    /// <summary>
    /// Gets the node type for icon binding.
    /// </summary>
    public Type NodeType => typeof(SceneNode);

    /// <summary>
    /// Gets or sets the current node (single-selection only).
    /// </summary>
    public SceneNode? Node
    {
        get => this.node;
        set
        {
            if (ReferenceEquals(this.node, value))
            {
                return;
            }

            var oldNodeName = this.node?.Name;
            var newNodeName = value?.Name;
            this.LogNodeChanged(oldNodeName, newNodeName);

            if (this.node is not null)
            {
                this.node.PropertyChanged -= OnNodePropertyChanged;
            }

            this.node = value;

            if (this.node is not null)
            {
                this.node.PropertyChanged += OnNodePropertyChanged;
            }

            this.SyncFromNode();
            this.OnPropertyChanged();

            this.DeleteComponentCommand.NotifyCanExecuteChanged();
            this.AddGeometryCommand.NotifyCanExecuteChanged();
            this.AddPerspectiveCameraCommand.NotifyCanExecuteChanged();
            this.AddOrthographicCameraCommand.NotifyCanExecuteChanged();

            return;

            void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
            {
                if (e.PropertyName == nameof(SceneNode.Name))
                {
                    this.EnqueueUi(() =>
                    {
                        this.doNotPropagateName = true;
                        this.Name = this.node?.Name;
                        this.doNotPropagateName = false;
                    });
                }
            }
        }
    }

    public ObservableCollection<GameComponent> Components { get; } = new();

    [ObservableProperty]
    public partial string? Name { get; set; }

    partial void OnNameChanged(string? value)
    {
        if (this.doNotPropagateName)
        {
            return;
        }

        if (this.Node is null || value is null)
        {
            return;
        }

        this.Node.Name = value;
    }

    private IMenuSource? addComponentMenu;

    public IMenuSource AddComponentMenu => this.addComponentMenu ??= this.BuildAddComponentMenu();

    private IMenuSource BuildAddComponentMenu()
    {
        var builder = new MenuBuilder();
        builder.AddMenuItem("Geometry", this.AddGeometryCommand);
        builder.AddMenuItem("Perspective Camera", this.AddPerspectiveCameraCommand);
        builder.AddMenuItem("Orthographic Camera", this.AddOrthographicCameraCommand);
        return builder.Build();
    }

    [RelayCommand(CanExecute = nameof(CanAddGeometry))]
    private void AddGeometry() => this.AddComponent("Geometry");

    private bool CanAddGeometry() => this.CanAddComponent("Geometry");

    [RelayCommand(CanExecute = nameof(CanAddPerspectiveCamera))]
    private void AddPerspectiveCamera() => this.AddComponent("PerspectiveCamera");

    private bool CanAddPerspectiveCamera() => this.CanAddComponent("PerspectiveCamera");

    [RelayCommand(CanExecute = nameof(CanAddOrthographicCamera))]
    private void AddOrthographicCamera() => this.AddComponent("OrthographicCamera");

    private bool CanAddOrthographicCamera() => this.CanAddComponent("OrthographicCamera");

    [RelayCommand(CanExecute = nameof(CanDeleteComponent))]
    private void DeleteComponent(GameComponent? comp)
    {
        if (this.Node is null || comp is null || comp.IsLocked)
        {
            return;
        }

        this.ApplyDeleteComponent(new ComponentOperation(this.Node, comp));
    }

    private bool CanDeleteComponent(GameComponent? comp) => this.Node is not null && comp is not null && comp.IsLocked == false;

    private void AddComponent(string typeId)
    {
        if (this.Node is null)
        {
            return;
        }

        this.LogAddComponentRequested(typeId, this.Node.Name, this.Components.Count, null);

        var comp = CreateComponentFromId(typeId);
        if (comp is null)
        {
            this.LogAddComponentFactoryFailed(typeId);
            return;
        }

        this.ApplyAddComponent(new ComponentOperation(this.Node, comp));
    }

    private void ApplyAddComponent(ComponentOperation? op)
    {
        ArgumentNullException.ThrowIfNull(op);
        this.EnqueueUi(() =>
        {
            try
            {
                var nodeComponentsBefore = op.Node.Components.Count;
                var vmCountBefore = this.Components.Count;
                this.LogApplyAddComponent(op.Node.Name, op.Component.GetType().Name, op.Component.Name, nodeComponentsBefore, vmCountBefore);

                var added = op.Node.AddComponent(op.Component);
                if (added && !this.Components.Contains(op.Component))
                {
                    this.Components.Add(op.Component);
                }

                this.LogApplyAddComponentResult(added, op.Node.Components.Count, this.Components.Count);
            }
            catch (Exception ex)
            {
                this.LogUnexpectedException(nameof(ApplyAddComponent), ex);
                throw;
            }
        });

        var historyKeeper = UndoRedo.Default[this.HistoryRoot ?? this];
        historyKeeper.AddChange($"Remove Component ({op.Component.Name})", this.ApplyDeleteComponent, op);
    }

    private void ApplyDeleteComponent(ComponentOperation? op)
    {
        ArgumentNullException.ThrowIfNull(op);
        this.EnqueueUi(() =>
        {
            try
            {
                var nodeComponentsBefore = op.Node.Components.Count;
                var vmCountBefore = this.Components.Count;
                this.LogApplyDeleteComponent(op.Node.Name, op.Component.GetType().Name, op.Component.Name, nodeComponentsBefore, vmCountBefore, null);

                var removed = op.Node.RemoveComponent(op.Component);
                if (!removed)
                {
                    this.LogApplyDeleteComponentResult(false, op.Node.Components.Count, this.Components.Count, null);
                    return;
                }

                _ = this.Components.Remove(op.Component);
                this.LogApplyDeleteComponentResult(true, op.Node.Components.Count, this.Components.Count, null);
            }
            catch (Exception ex)
            {
                this.LogUnexpectedException(nameof(ApplyDeleteComponent), ex);
                throw;
            }
        });

        var historyKeeper = UndoRedo.Default[this.HistoryRoot ?? this];
        historyKeeper.AddChange($"Restore Component ({op.Component.Name})", this.ApplyAddComponent, op);
    }

    private void SyncFromNode()
    {
        this.EnqueueUi(() =>
        {
            try
            {
                this.Components.Clear();

                this.doNotPropagateName = true;
                this.Name = this.Node?.Name;
                this.doNotPropagateName = false;

                if (this.Node?.Components is null)
                {
                    return;
                }

                foreach (var component in this.Node.Components)
                {
                    this.Components.Add(component);
                }
            }
            catch (Exception ex)
            {
                this.LogUnexpectedException(nameof(SyncFromNode), ex);
                throw;
            }
        });
    }

    private void EnqueueUi(Action action)
    {
        if (this.dispatcher is null || this.dispatcher.HasThreadAccess)
        {
            action();
            return;
        }

        _ = this.dispatcher.TryEnqueue(() => action());
    }

    private bool CanAddComponent(string typeId)
        => this.Node is not null && typeId is not null && !string.Equals(typeId, "Transform", StringComparison.OrdinalIgnoreCase);

    private static GameComponent? CreateComponentFromId(string typeId)
        => typeId switch
        {
            "Geometry" => new GeometryComponent { Name = "Geometry" },
            "PerspectiveCamera" => new PerspectiveCamera { Name = "Perspective Camera" },
            "OrthographicCamera" => new OrthographicCamera { Name = "Orthographic Camera" },
            _ => null,
        };
}
