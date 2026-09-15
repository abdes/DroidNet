// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Inspector;

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
    /// Identifies the <see cref="HistoryRoot"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty HistoryRootProperty = DependencyProperty.Register(
        nameof(HistoryRoot),
        typeof(object),
        typeof(SceneNodeDetailsView),
        new PropertyMetadata(defaultValue: null, OnHistoryRootChanged));

    /// <summary>
    /// Identifies the <see cref="LoggerFactory"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty = DependencyProperty.Register(
        nameof(LoggerFactory),
        typeof(ILoggerFactory),
        typeof(SceneNodeDetailsView),
        new PropertyMetadata(defaultValue: null, OnLoggerFactoryChanged));

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeDetailsView"/> class.
    /// </summary>
    public SceneNodeDetailsView()
    {
        this.ViewModel = new SceneNodeDetailsViewModel();
        this.InitializeComponent();

        this.ViewModel.Node = this.Node;
        this.ViewModel.HistoryRoot = this.HistoryRoot;
        this.ViewModel.LoggerFactory = this.LoggerFactory;
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
    /// Gets or sets the undo/redo history root to use when recording changes. Typically bound to the parent
    /// <see cref="SceneNodeEditorViewModel"/> instance so inspector changes share one stack.
    /// </summary>
    public object? HistoryRoot
    {
        get => this.GetValue(HistoryRootProperty);
        set => this.SetValue(HistoryRootProperty, value);
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

    private static void OnHistoryRootChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not SceneNodeDetailsView view)
        {
            return;
        }

        view.ViewModel!.HistoryRoot = e.NewValue;
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

    private void OnDeleteComponentClicked(object sender, RoutedEventArgs e)
        => _ = this.DeleteSelectedComponent();
}
