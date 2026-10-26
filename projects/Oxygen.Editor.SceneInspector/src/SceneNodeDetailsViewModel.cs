// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Menus;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector;

/// <summary>
/// ViewModel for <see cref="SceneNodeDetailsView"/>. This ViewModel assumes a single
/// <see cref="SceneNode"/> selection and exposes node-level properties and component
/// operations such as adding or removing components.
/// </summary>
/// <remarks>
/// The view model wires to the <c>PropertyChanged</c> events of the current <see cref="Node"/>'s
/// <see cref="MetadataComponent"/> to keep UI-bound properties (for example <see cref="Name"/>)
/// in sync. Renames and component add/remove requests go to <see cref="NodeEdits"/>, so that the editor host performs the
/// actual mutations and records them in the scene's history.
/// </remarks>
public sealed partial class SceneNodeDetailsViewModel : ObservableObject
{
    private readonly DispatcherQueue? dispatcher;

    private ILogger logger;

    private INotifyCollectionChanged? componentCollectionNotifier;

    private bool doNotPropagateName;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeDetailsViewModel"/> class.
    /// </summary>
    /// <param name="loggerFactory">Optional logger factory used to create an <see cref="ILogger"/>.
    /// If <see langword="null"/>, nothing is logged.</param>
    public SceneNodeDetailsViewModel(ILoggerFactory? loggerFactory = null)
    {
        this.dispatcher = DispatcherQueue.GetForCurrentThread();
        this.logger = NullLogger<SceneNodeDetailsViewModel>.Instance;
        this.LoggerFactory = loggerFactory;
        this.LogConstructed(this.dispatcher is not null);
    }

    /// <summary>Gets or sets what renames the node and adds and removes its components, typically the parent inspector.</summary>
    public ISceneNodeEdits? NodeEdits { get; set; }

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
            this.logger = value?.CreateLogger<SceneNodeDetailsViewModel>() ?? NullLogger<SceneNodeDetailsViewModel>.Instance;
        }
    }

    /// <summary>
    /// Gets the node type for icon binding. Returns the runtime type of the current
    /// <see cref="Node"/>, or <see cref="SceneNode"/> when no node is selected.
    /// </summary>
    public Type NodeType => this.Node?.GetType() ?? typeof(SceneNode);

    /// <summary>
    /// Gets or sets the current <see cref="SceneNode"/>. This view model assumes a single selection.
    /// When set, the view model subscribes to the <see cref="INotifyPropertyChanged"/> events of the node's
    /// metadata component to keep view-model properties synchronized with changes originating from the model.
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

            var oldNodeName = field?.Metadata.Name;
            var newNodeName = value?.Metadata.Name;
            this.LogNodeChanged(oldNodeName, newNodeName);

            field?.Metadata.PropertyChanged -= OnNamePropertyChanged;
            this.componentCollectionNotifier?.CollectionChanged -= this.OnNodeComponentsChanged;
            this.componentCollectionNotifier = null;

            field = value;
            this.SelectedComponent = null;

            if (field is not null)
            {
                field.Metadata.PropertyChanged += OnNamePropertyChanged;
                if (field.Components is INotifyCollectionChanged componentNotifier)
                {
                    this.componentCollectionNotifier = componentNotifier;
                    this.componentCollectionNotifier.CollectionChanged += this.OnNodeComponentsChanged;
                }
            }

            this.SyncFromNode();
            this.OnPropertyChanged();

            this.DeleteComponentCommand.NotifyCanExecuteChanged();
            this.AddGeometryCommand.NotifyCanExecuteChanged();
            this.AddPerspectiveCameraCommand.NotifyCanExecuteChanged();
            this.AddOrthographicCameraCommand.NotifyCanExecuteChanged();
            this.AddDirectionalLightCommand.NotifyCanExecuteChanged();
            this.AddPointLightCommand.NotifyCanExecuteChanged();
            this.AddLocalFogVolumeCommand.NotifyCanExecuteChanged();
            this.AddScriptingCommand.NotifyCanExecuteChanged();
            this.AddSpotLightCommand.NotifyCanExecuteChanged();

            return;

            void OnNamePropertyChanged(object? sender, PropertyChangedEventArgs e)
            {
                if (string.Equals(e.PropertyName, nameof(MetadataComponent.Name), StringComparison.Ordinal))
                {
                    _ = this.dispatcher?.DispatchAsync(() =>
                    {
                        this.doNotPropagateName = true;
                        this.Name = field?.Metadata.Name;
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
    /// Gets or sets the component currently selected in the component list.
    /// </summary>
    [ObservableProperty]
    public partial GameComponent? SelectedComponent { get; set; }

    /// <summary>
    /// Gets or sets the instance and source of a node an instance brings in, such as "in Car3 · Car.glb (model)";
    /// <see langword="null"/> for a node of the scene. A node an instance brings in keeps its source's name and
    /// components.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsInstanced))]
    [NotifyPropertyChangedFor(nameof(IsOwned))]
    [NotifyPropertyChangedFor(nameof(CanAddComponents))]
    [NotifyPropertyChangedFor(nameof(CanDeleteSelectedComponent))]
    public partial string? Origin { get; set; }

    /// <summary>Gets or sets why a node an instance brings in refuses structural commands, shown on the disabled ones.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(AddComponentToolTip))]
    [NotifyPropertyChangedFor(nameof(RemoveComponentToolTip))]
    public partial string? StructureRefusal { get; set; }

    /// <summary>Gets or sets what selects the instance root of a node an instance brings in.</summary>
    [ObservableProperty]
    public partial ICommand? SelectInstanceCommand { get; set; }

    /// <summary>Gets a value indicating whether the node is one an instance brings in.</summary>
    public bool IsInstanced => this.Origin is not null;

    /// <summary>Gets a value indicating whether the node is a node of the scene, whose name can be edited.</summary>
    public bool IsOwned => !this.IsInstanced;

    /// <summary>Gets a value indicating whether components can be added: not to a node an instance brings in.</summary>
    public bool CanAddComponents => !this.IsInstanced;

    /// <summary>Gets the tooltip of <i>Add component</i>: the refusal when it is disabled.</summary>
    public string AddComponentToolTip => this.StructureRefusal ?? "Add component";

    /// <summary>Gets the tooltip of <i>Remove component</i>: the refusal when it is disabled.</summary>
    public string RemoveComponentToolTip => this.StructureRefusal ?? "Remove selected component";

    /// <summary>
    /// Gets a value indicating whether the current component selection can be removed.
    /// </summary>
    public bool CanDeleteSelectedComponent => this.CanDeleteComponent(this.SelectedComponent);

    /// <summary>
    /// Gets a menu source used by the UI to expose available component types that can be added.
    /// The menu items are bound to commands on this view-model.
    /// </summary>
    public IMenuSource AddComponentMenu => field ??= this.BuildAddComponentMenu();

    // The scene's authoring renames the node, as one step of its history; a refused name shows the node's own again.
    partial void OnNameChanged(string? value)
    {
        if (this.doNotPropagateName || this.IsInstanced || this.Node is not { } node || value is null || string.Equals(value, node.Metadata.Name, StringComparison.Ordinal))
        {
            return;
        }

        _ = this.RenameAsync(node, value);
    }

    partial void OnSelectedComponentChanged(GameComponent? value)
    {
        _ = value;
        this.DeleteComponentCommand.NotifyCanExecuteChanged();
        this.OnPropertyChanged(nameof(this.CanDeleteSelectedComponent));
    }

    partial void OnOriginChanged(string? value)
    {
        _ = value;
        this.NotifyComponentCommandsChanged();
    }

    private IMenuSource BuildAddComponentMenu()
        => new MenuBuilder()
            .AddMenuItem("Geometry", this.AddGeometryCommand)
            .AddMenuItem("Perspective Camera", this.AddPerspectiveCameraCommand)
            .AddMenuItem("Orthographic Camera", this.AddOrthographicCameraCommand)
            .AddSeparator()
            .AddSubmenu("Light", lights =>
            {
                _ = lights.AddMenuItem("Directional Light", this.AddDirectionalLightCommand);
                _ = lights.AddMenuItem("Point Light", this.AddPointLightCommand);
                _ = lights.AddMenuItem("Spot Light", this.AddSpotLightCommand);
            })
            .AddMenuItem("Local Fog Volume", this.AddLocalFogVolumeCommand)
            .AddSeparator()
            .AddMenuItem(ScriptingComponent.DefaultName, this.AddScriptingCommand)
            .Build();

    [RelayCommand(CanExecute = nameof(CanAddGeometry))]
    private void AddGeometry() => this.AddComponent(SceneComponents.Geometry);

    private bool CanAddGeometry() => this.CanAddComponents && this.Node is not null && this.Node.Components.OfType<GeometryComponent>().FirstOrDefault() is null;

    [RelayCommand(CanExecute = nameof(CanAddPerspectiveCamera))]
    private void AddPerspectiveCamera() => this.AddComponent(SceneComponents.PerspectiveCamera);

    private bool CanAddPerspectiveCamera() => this.CanAddCamera();

    [RelayCommand(CanExecute = nameof(CanAddOrthographicCamera))]
    private void AddOrthographicCamera() => this.AddComponent(SceneComponents.OrthographicCamera);

    private bool CanAddOrthographicCamera() => this.CanAddCamera();

    [RelayCommand(CanExecute = nameof(CanAddLight))]
    private void AddDirectionalLight() => this.AddComponent(SceneComponents.DirectionalLight);

    [RelayCommand(CanExecute = nameof(CanAddLight))]
    private void AddPointLight() => this.AddComponent(SceneComponents.PointLight);

    [RelayCommand(CanExecute = nameof(CanAddLight))]
    private void AddSpotLight() => this.AddComponent(SceneComponents.SpotLight);

    private bool CanAddLight() => this.CanAddComponents && this.Node is not null && this.Node.Components.OfType<LightComponent>().FirstOrDefault() is null;

    [RelayCommand(CanExecute = nameof(CanAddLocalFogVolume))]
    private void AddLocalFogVolume() => this.AddComponent(SceneComponents.LocalFogVolume);

    private bool CanAddLocalFogVolume() => this.CanAddComponents && this.Node is not null && !this.Node.Components.OfType<LocalFogVolumeComponent>().Any();

    [RelayCommand(CanExecute = nameof(CanAddScripting))]
    private void AddScripting() => this.AddComponent(SceneComponents.Scripting);

    private bool CanAddScripting() => this.CanAddComponents && this.Node is not null && !this.Node.Components.OfType<ScriptingComponent>().Any();

    private bool CanAddCamera() => this.CanAddComponents && this.Node is not null && this.Node.Components.OfType<CameraComponent>().FirstOrDefault() is null;

    [RelayCommand(CanExecute = nameof(CanDeleteComponent))]
    private void DeleteComponent(GameComponent? comp)
    {
        if (this.Node is null || !this.IsCurrentComponent(comp) || comp?.IsLocked != false)
        {
            return;
        }

        _ = this.NodeEdits?.RemoveComponentAsync(this.Node, comp);
    }

    private bool CanDeleteComponent(GameComponent? comp) => !this.IsInstanced && this.Node is not null && this.IsCurrentComponent(comp) && comp?.IsLocked == false;

    // Scenes creates the component, with its defaults, from its schema kind.
    private void AddComponent(string kind)
    {
        if (this.Node is null)
        {
            return;
        }

        this.LogAddComponentRequested(kind, this.Node.Metadata.Name, this.Node.Components.Count, selectedComponentName: null);

        if (kind is SceneComponents.DirectionalLight or SceneComponents.PointLight or SceneComponents.SpotLight
            && !this.CanAddLight())
        {
            return;
        }

        // The inspector, the single mutator of the node, performs the add.
        _ = this.NodeEdits?.AddComponentAsync(this.Node, kind);
    }

    private async Task RenameAsync(SceneNode node, string name)
    {
        if (this.NodeEdits is { } edits && await edits.RenameAsync(node, name).ConfigureAwait(true))
        {
            return;
        }

        if (ReferenceEquals(this.Node, node))
        {
            this.doNotPropagateName = true;
            this.Name = node.Metadata.Name;
            this.doNotPropagateName = false;
        }
    }

    private void SyncFromNode()
        => _ = this.dispatcher?.DispatchAsync(() =>
        {
            try
            {
                this.doNotPropagateName = true;
                this.Name = this.Node?.Metadata.Name;
                this.doNotPropagateName = false;
            }
            catch (Exception ex)
            {
                this.LogUnexpectedException(nameof(this.SyncFromNode), ex);
                throw;
            }
        });

    private bool CanAddComponent(string typeId)
        => typeId switch
        {
            "Geometry" => this.CanAddGeometry(),
            "PerspectiveCamera" or "OrthographicCamera" => this.CanAddCamera(),
            "DirectionalLight" or "PointLight" or "SpotLight" => this.CanAddLight(),
            "LocalFogVolume" => this.CanAddLocalFogVolume(),
            "Scripting" => this.CanAddScripting(),
            _ => false,
        };

    private bool IsCurrentComponent(GameComponent? component)
        => component is not null && this.Node?.Components.Contains(component) == true;

    private void OnNodeComponentsChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        _ = sender;
        _ = e;
        if (!this.IsCurrentComponent(this.SelectedComponent))
        {
            this.SelectedComponent = null;
        }

        this.NotifyComponentCommandsChanged();
    }

    private void NotifyComponentCommandsChanged()
    {
        this.DeleteComponentCommand.NotifyCanExecuteChanged();
        this.OnPropertyChanged(nameof(this.CanDeleteSelectedComponent));
        this.AddGeometryCommand.NotifyCanExecuteChanged();
        this.AddPerspectiveCameraCommand.NotifyCanExecuteChanged();
        this.AddOrthographicCameraCommand.NotifyCanExecuteChanged();
        this.AddDirectionalLightCommand.NotifyCanExecuteChanged();
        this.AddPointLightCommand.NotifyCanExecuteChanged();
        this.AddLocalFogVolumeCommand.NotifyCanExecuteChanged();
        this.AddScriptingCommand.NotifyCanExecuteChanged();
        this.AddSpotLightCommand.NotifyCanExecuteChanged();
    }
}
