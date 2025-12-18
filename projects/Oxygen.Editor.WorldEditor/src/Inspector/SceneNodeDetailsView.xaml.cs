// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
/// Single-selection-only SceneNode details view (header + components list).
/// </summary>
[ViewModel(typeof(SceneNodeDetailsViewModel))]
public sealed partial class SceneNodeDetailsView : UserControl
{
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
    }

    private static void OnHistoryRootChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not SceneNodeDetailsView view)
        {
            return;
        }

        view.ViewModel!.HistoryRoot = e.NewValue;
    }

    private void OnComponentsListView_PreviewKeyDown(object sender, Microsoft.UI.Xaml.Input.KeyRoutedEventArgs e)
    {
        if (this.ViewModel is null)
        {
            return;
        }

        if (e.Key == Windows.System.VirtualKey.Delete)
        {
            var itemsView = sender as ItemsView;
            var comp = itemsView?.SelectedItem as GameComponent;

            if (comp is not null && this.ViewModel.DeleteComponentCommand.CanExecute(comp))
            {
                // Prevent the view from holding on to a soon-to-be-removed selected item.
                itemsView?.DeselectAll();
                this.ViewModel.DeleteComponentCommand.Execute(comp);
                e.Handled = true;
            }
        }
    }

    private void OnComponentsItemsView_SelectionChanged(ItemsView sender, ItemsViewSelectionChangedEventArgs args)
    {
        if (this.ViewModel is null)
        {
            return;
        }

        _ = sender;
        _ = args;
        this.ViewModel.DeleteComponentCommand.NotifyCanExecuteChanged();
    }
}
