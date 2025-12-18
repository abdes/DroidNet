// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls.Menus;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
/// ViewModel for <see cref="SceneNodeDetailsView"/>. This ViewModel assumes a single
/// <see cref="SceneNode"/> selection and exposes node-level properties and component
/// operations such as adding or removing components.
/// </summary>
/// <remarks>
/// The view model wires to the <c>SceneNode.PropertyChanged</c> events of the
/// current <see cref="Node"/> to keep UI-bound properties (for example <see cref="Name"/>)
/// in sync. Component add/remove requests are emitted via the weak-reference messenger so
/// that the editor host can perform the actual mutations and maintain undo/redo history.
/// </remarks>
public sealed partial class SceneNodeDetailsViewModel : ObservableObject
{
    private readonly DispatcherQueue? dispatcher;

    private ILogger logger;

    private bool doNotPropagateName;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeDetailsViewModel"/> class.
    /// </summary>
    /// <param name="loggerFactory">Optional logger factory used to create an <see cref="ILogger"/>.
    /// If <see langword="null"/>, a <see cref="NullLoggerFactory"/> is used.</param>
    public SceneNodeDetailsViewModel(ILoggerFactory? loggerFactory = null)
    {
        this.dispatcher = DispatcherQueue.GetForCurrentThread();
        this.logger = NullLoggerFactory.Instance.CreateLogger<SceneNodeDetailsViewModel>();
        this.LoggerFactory = loggerFactory;
        this.LogConstructed(this.dispatcher is not null);
    }

    /// <summary>
    /// Gets or sets the object used as the undo/redo history root.
    /// When set (typically to the parent inspector ViewModel), changes are recorded into that history.
    /// </summary>
    public object? HistoryRoot { get; set; }

    /// <summary>
    /// Gets or sets the <see cref="ILoggerFactory"/> used by this view-model.
    /// Setting this updates the internal logger instance used for diagnostics.
    /// </summary>
    public ILoggerFactory? LoggerFactory
    {
        get;
        set
        {
            if (ReferenceEquals(field, value))
            {
                return;
            }

            field = value;
            this.logger = (value ?? NullLoggerFactory.Instance).CreateLogger<SceneNodeDetailsViewModel>();
        }
    }

    /// <summary>
    /// Gets the node type for icon binding. Returns the runtime type of the current
    /// <see cref="Node"/>, or <see cref="SceneNode"/> when no node is selected.
    /// </summary>
    public Type NodeType => this.Node?.GetType() ?? typeof(SceneNode);

    /// <summary>
    /// Gets or sets the current <see cref="SceneNode"/>. This view model assumes a single selection.
    /// When set, the view model subscribes to the node's <see cref="INotifyPropertyChanged"/>
    /// events to keep view-model properties synchronized with changes originating from the model.
    /// </summary>
    public SceneNode? Node
    {
        get;
        set
        {
            if (ReferenceEquals(field, value))
            {
                return;
            }

            var oldNodeName = field?.Name;
            var newNodeName = value?.Name;
            this.LogNodeChanged(oldNodeName, newNodeName);

            if (field is not null)
            {
                field.PropertyChanged -= OnNodePropertyChanged;
            }

            field = value;

            if (field is not null)
            {
                field.PropertyChanged += OnNodePropertyChanged;
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
                if (string.Equals(e.PropertyName, nameof(SceneNode.Name), StringComparison.Ordinal))
                {
                    this.EnqueueUi(() =>
                    {
                        this.doNotPropagateName = true;
                        this.Name = field?.Name;
                        this.doNotPropagateName = false;
                    });
                }
            }
        }
    }

    // The UI now binds directly to SceneNode.Components (observable). We no longer keep a separate collection here.

    /// <summary>
    /// Gets or sets the node name shown in the UI. Changes are propagated back to the underlying
    /// <see cref="Node"/> unless the synchronization flag is set (used to prevent feedback loops).
    /// </summary>
    [ObservableProperty]
    public partial string? Name { get; set; }

    /// <summary>
    /// Gets a menu source used by the UI to expose available component types that can be added.
    /// The menu items are bound to commands on this view-model.
    /// </summary>
    public IMenuSource AddComponentMenu => field ??= this.BuildAddComponentMenu();

    private static GameComponent? CreateComponentFromId(string typeId)
        => typeId switch
        {
            "Geometry" => new GeometryComponent { Name = "Geometry" },
            "PerspectiveCamera" => new PerspectiveCamera { Name = "Perspective Camera" },
            "OrthographicCamera" => new OrthographicCamera { Name = "Orthographic Camera" },
            _ => null,
        };

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

    private IMenuSource BuildAddComponentMenu()
        => new MenuBuilder()
            .AddMenuItem("Geometry", this.AddGeometryCommand)
            .AddMenuItem("Perspective Camera", this.AddPerspectiveCameraCommand)
            .AddMenuItem("Orthographic Camera", this.AddOrthographicCameraCommand)
            .Build();

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
        if (this.Node is null || comp?.IsLocked != false)
        {
            return;
        }

        WeakReferenceMessenger.Default.Send(new Messages.ComponentRemoveRequestedMessage(this.Node, comp));
    }

    private bool CanDeleteComponent(GameComponent? comp) => this.Node is not null && comp?.IsLocked == false;

    private void AddComponent(string typeId)
    {
        if (this.Node is null)
        {
            return;
        }

        this.LogAddComponentRequested(typeId, this.Node.Name, this.Node.Components?.Count ?? 0, selectedComponentName: null);

        var comp = CreateComponentFromId(typeId);
        if (comp is null)
        {
            this.LogAddComponentFactoryFailed(typeId);
            return;
        }

        // Request the editor (single mutator) to perform the add; decoupled via messenger.
        WeakReferenceMessenger.Default.Send(new Messages.ComponentAddRequestedMessage(this.Node, comp));
    }

    private void SyncFromNode()
        => this.EnqueueUi(() =>
        {
            try
            {
                this.doNotPropagateName = true;
                this.Name = this.Node?.Name;
                this.doNotPropagateName = false;
            }
            catch (Exception ex)
            {
                this.LogUnexpectedException(nameof(this.SyncFromNode), ex);
                throw;
            }
        });

    private void EnqueueUi(Action action)
    {
        if (this.dispatcher?.HasThreadAccess != false)
        {
            action();
            return;
        }

        _ = this.dispatcher.TryEnqueue(() => action());
    }

    private bool CanAddComponent(string typeId)
        => this.Node is not null && typeId is not null && !string.Equals(typeId, "Transform", StringComparison.OrdinalIgnoreCase);

    private sealed record ComponentOperation(SceneNode Node, GameComponent Component);
}
