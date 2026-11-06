// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Displays the Content mounts dialog: the mount list and the form that adds one.</summary>
[ViewModel(typeof(ContentMountsViewModel))]
public sealed partial class ContentMountsView
{
    /// <summary>Initializes a new instance of the <see cref="ContentMountsView"/> class.</summary>
    public ContentMountsView()
    {
        this.InitializeComponent();
        this.Loaded += (_, _) => this.ViewModel?.Mounts.CollectionChanged += this.OnMountsChanged;
        this.Unloaded += (_, _) => this.ViewModel?.Mounts.CollectionChanged -= this.OnMountsChanged;
    }

    /// <summary>Hides what shows only when a value is false.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The visibility.</returns>
    public static Visibility CollapsedWhen(bool value) => value ? Visibility.Collapsed : Visibility.Visible;

    // A mount just added may land below the part of the list in view; its row exists once the list has laid it out.
    private void OnMountsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (args.Action == NotifyCollectionChangedAction.Add && args.NewItems?[0] is { } added)
        {
            _ = this.DispatcherQueue.TryEnqueue(() => (this.MountList.ContainerFromItem(added) as FrameworkElement)?.StartBringIntoView());
        }
    }
}
