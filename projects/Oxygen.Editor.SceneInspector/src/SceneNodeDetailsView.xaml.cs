// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using DroidNet.Controls.Menus;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector;

/// <summary>
/// Compact single-node header and component actions.
/// </summary>
[ViewModel(typeof(SceneNodeDetailsViewModel))]
public sealed partial class SceneNodeDetailsView : UserControl
{
    /// <summary>Identifies the component selected by the inspector's type filter.</summary>
    public static readonly DependencyProperty SelectedComponentProperty = DependencyProperty.Register(
        nameof(SelectedComponent), typeof(GameComponent), typeof(SceneNodeDetailsView), new PropertyMetadata(defaultValue: null, OnSelectedComponentChanged));

    /// <summary>Identifies whether all applicable component properties are displayed.</summary>
    public static readonly DependencyProperty IsAllComponentsSelectedProperty = DependencyProperty.Register(
        nameof(IsAllComponentsSelected), typeof(bool), typeof(SceneNodeDetailsView), new PropertyMetadata(defaultValue: true));

    /// <summary>
    /// Identifies the <see cref="Node"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty NodeProperty = DependencyProperty.Register(
        nameof(Node),
        typeof(Oxygen.Editor.World.SceneNode),
        typeof(SceneNodeDetailsView),
        new PropertyMetadata(defaultValue: null, OnNodeChanged));

    /// <summary>
    /// Identifies the <see cref="NodeEdits"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty NodeEditsProperty = DependencyProperty.Register(
        nameof(NodeEdits),
        typeof(ISceneNodeEdits),
        typeof(SceneNodeDetailsView),
        new PropertyMetadata(defaultValue: null, OnNodeEditsChanged));

    /// <summary>
    /// Identifies the <see cref="LoggerFactory"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty = DependencyProperty.Register(
        nameof(LoggerFactory),
        typeof(ILoggerFactory),
        typeof(SceneNodeDetailsView),
        new PropertyMetadata(defaultValue: null, OnLoggerFactoryChanged));

    /// <summary>Identifies the <see cref="Origin"/> dependency property.</summary>
    public static readonly DependencyProperty OriginProperty = DependencyProperty.Register(
        nameof(Origin), typeof(string), typeof(SceneNodeDetailsView), new PropertyMetadata(defaultValue: null, OnOriginChanged));

    /// <summary>Identifies the <see cref="StructureRefusal"/> dependency property.</summary>
    public static readonly DependencyProperty StructureRefusalProperty = DependencyProperty.Register(
        nameof(StructureRefusal), typeof(string), typeof(SceneNodeDetailsView), new PropertyMetadata(defaultValue: null, OnStructureRefusalChanged));

    /// <summary>Identifies the <see cref="SelectInstanceCommand"/> dependency property.</summary>
    public static readonly DependencyProperty SelectInstanceCommandProperty = DependencyProperty.Register(
        nameof(SelectInstanceCommand), typeof(ICommand), typeof(SceneNodeDetailsView), new PropertyMetadata(defaultValue: null, OnSelectInstanceCommandChanged));

    /// <summary>Identifies the <see cref="ApplyMenu"/> dependency property.</summary>
    public static readonly DependencyProperty ApplyMenuProperty = DependencyProperty.Register(
        nameof(ApplyMenu), typeof(IMenuSource), typeof(SceneNodeDetailsView), new PropertyMetadata(defaultValue: null, OnApplyMenuChanged));

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeDetailsView"/> class.
    /// </summary>
    public SceneNodeDetailsView()
    {
        this.ViewModel = new SceneNodeDetailsViewModel();
        this.InitializeComponent();

        this.ViewModel.Node = this.Node;
        this.ViewModel.NodeEdits = this.NodeEdits;
        this.ViewModel.LoggerFactory = this.LoggerFactory;
        this.ViewModel.Origin = NullIfEmpty(this.Origin);
        this.ViewModel.StructureRefusal = NullIfEmpty(this.StructureRefusal);
        this.ViewModel.SelectInstanceCommand = this.SelectInstanceCommand;
        this.ViewModel.ApplyMenu = this.ApplyMenu;
    }

    /// <summary>Occurs when the user asks to clear the component filter.</summary>
    public event EventHandler? AllComponentsRequested;

    /// <summary>Gets or sets the component selected for single-node actions.</summary>
    public GameComponent? SelectedComponent
    {
        get => (GameComponent?)this.GetValue(SelectedComponentProperty);
        set => this.SetValue(SelectedComponentProperty, value);
    }

    /// <summary>Gets or sets a value indicating whether the All filter is active.</summary>
    public bool IsAllComponentsSelected
    {
        get => (bool)this.GetValue(IsAllComponentsSelectedProperty);
        set => this.SetValue(IsAllComponentsSelectedProperty, value);
    }

    /// <summary>
    /// Gets or sets the scene node to display. This control is intended to be used only when a single node is selected.
    /// </summary>
    public Oxygen.Editor.World.SceneNode? Node
    {
        get => (Oxygen.Editor.World.SceneNode?)this.GetValue(NodeProperty);
        set => this.SetValue(NodeProperty, value);
    }

    /// <summary>
    /// Gets or sets what adds and removes the node's components. Typically bound to the parent
    /// <see cref="SceneInspectorViewModel"/>, so each change is one step of the scene's history.
    /// </summary>
    public ISceneNodeEdits? NodeEdits
    {
        get => (ISceneNodeEdits?)this.GetValue(NodeEditsProperty);
        set => this.SetValue(NodeEditsProperty, value);
    }

    /// <summary>
    /// Gets or sets the <see cref="ILoggerFactory"/> used to construct the view-model.
    /// This allows the view-model to participate in the app's logging pipeline.
    /// </summary>
    public ILoggerFactory? LoggerFactory
    {
        get => (ILoggerFactory?)this.GetValue(LoggerFactoryProperty);
        set => this.SetValue(LoggerFactoryProperty, value);
    }

    /// <summary>
    /// Gets or sets the instance and source of a node an instance brings in, shown under its name; <see langword="null"/>
    /// for a node of the scene.
    /// </summary>
    public string? Origin
    {
        get => (string?)this.GetValue(OriginProperty);
        set => this.SetValue(OriginProperty, value);
    }

    /// <summary>Gets or sets why a node an instance brings in refuses structural commands.</summary>
    public string? StructureRefusal
    {
        get => (string?)this.GetValue(StructureRefusalProperty);
        set => this.SetValue(StructureRefusalProperty, value);
    }

    /// <summary>Gets or sets what selects the instance root of a node an instance brings in.</summary>
    public ICommand? SelectInstanceCommand
    {
        get => (ICommand?)this.GetValue(SelectInstanceCommandProperty);
        set => this.SetValue(SelectInstanceCommandProperty, value);
    }

    /// <summary>
    /// Gets or sets the menu that applies the changes of a node an instance brings in to the authored scenes on its path;
    /// <see langword="null"/> when it has none to apply.
    /// </summary>
    public IMenuSource? ApplyMenu
    {
        get => (IMenuSource?)this.GetValue(ApplyMenuProperty);
        set => this.SetValue(ApplyMenuProperty, value);
    }

    /// <summary>Requests removal of the current unlocked component using the existing document command path.</summary>
    /// <returns>Whether an applicable removal was requested.</returns>
    public bool DeleteSelectedComponent()
    {
        var component = this.ViewModel?.SelectedComponent;
        if (component is null || this.ViewModel?.DeleteComponentCommand.CanExecute(component) != true)
        {
            return false;
        }

        this.ViewModel.DeleteComponentCommand.Execute(component);
        return true;
    }

    private static void OnLoggerFactoryChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not SceneNodeDetailsView view)
        {
            return;
        }

        _ = view.ViewModel?.LoggerFactory = (ILoggerFactory?)e.NewValue;
    }

    private static void OnNodeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not SceneNodeDetailsView view)
        {
            return;
        }

        view.ViewModel!.Node = (Oxygen.Editor.World.SceneNode?)e.NewValue;
        view.SyncSelectedComponent();
    }

    // x:Bind sets an empty string where its source is null.
    private static string? NullIfEmpty(string? value) => string.IsNullOrEmpty(value) ? null : value;

    private static void OnOriginChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is SceneNodeDetailsView view)
        {
            view.ViewModel!.Origin = NullIfEmpty((string?)e.NewValue);
        }
    }

    private static void OnStructureRefusalChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is SceneNodeDetailsView view)
        {
            view.ViewModel!.StructureRefusal = NullIfEmpty((string?)e.NewValue);
        }
    }

    private static void OnSelectInstanceCommandChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is SceneNodeDetailsView view)
        {
            view.ViewModel!.SelectInstanceCommand = (ICommand?)e.NewValue;
        }
    }

    private static void OnApplyMenuChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is SceneNodeDetailsView view)
        {
            view.ViewModel!.ApplyMenu = (IMenuSource?)e.NewValue;
        }
    }

    private static void OnNodeEditsChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not SceneNodeDetailsView view)
        {
            return;
        }

        view.ViewModel!.NodeEdits = (ISceneNodeEdits?)e.NewValue;
    }

    private static void OnSelectedComponentChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        var view = (SceneNodeDetailsView)sender;
        view.SyncSelectedComponent();
    }

    private void SyncSelectedComponent()
        => this.ViewModel!.SelectedComponent = this.SelectedComponent is { } selected && this.Node?.Components.Contains(selected) == true
            ? selected : null;

    private void OnAllComponentsChanged(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        if (sender is not ToggleButton button)
        {
            return;
        }

        if (button.IsChecked == true && !this.IsAllComponentsSelected)
        {
            this.AllComponentsRequested?.Invoke(this, EventArgs.Empty);
        }
        else if (button.IsChecked != true && this.IsAllComponentsSelected)
        {
            button.IsChecked = true;
        }
    }

    private void OnDeleteComponentClicked(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        ArgumentNullException.ThrowIfNull(sender);
        _ = this.DeleteSelectedComponent();
    }
}
