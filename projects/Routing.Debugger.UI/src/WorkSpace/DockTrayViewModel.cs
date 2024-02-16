// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Docking;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// The ViewModel for a docking tray control.
/// </summary>
public partial class DockTrayViewModel : ObservableObject, IDisposable
{
    private readonly ObservableCollection<IDockable> dockables = [];

    private readonly ReadOnlyObservableCollection<IDock> docks;
    private readonly IDisposable? changeSubscription;
    private readonly IDocker docker;

    public DockTrayViewModel(IDocker docker, IDockTray tray, Orientation orientation)
    {
        this.Dockables = new ReadOnlyObservableCollection<IDockable>(this.dockables);

        this.Orientation = orientation;
        this.docker = docker;
        this.docks = tray.MinimizedDocks;

        this.UpdateDockables();

        var dispatcherQueue = DispatcherQueue.GetForCurrentThread();

        // Create an observable for the docks collection
        var docksObservable
            = Observable.FromEventPattern<NotifyCollectionChangedEventHandler, NotifyCollectionChangedEventArgs>(
                handler => ((INotifyCollectionChanged)this.docks).CollectionChanged += handler,
                handler => ((INotifyCollectionChanged)this.docks).CollectionChanged -= handler);

        // Create observables for each dock in the docks collection
        var dockObservables = this.docks.Select(
            dock => Observable
                .FromEventPattern<NotifyCollectionChangedEventHandler, NotifyCollectionChangedEventArgs>(
                    handler => ((INotifyCollectionChanged)dock.Dockables).CollectionChanged += handler,
                    handler => ((INotifyCollectionChanged)dock.Dockables).CollectionChanged -= handler));

        // Merge the observables
        var mergedObservable = docksObservable.Merge(dockObservables.Merge());

        // Subscribe to the merged observable
        this.changeSubscription = mergedObservable
            .Throttle(TimeSpan.FromMilliseconds(500))
            .Subscribe(
                evt =>
                {
                    Debug.WriteLine($"Updating dockables because of: {evt.EventArgs.Action}");
                    dispatcherQueue.TryEnqueue(this.UpdateDockables);
                });
    }

    public Orientation Orientation { get; set; }

    public ReadOnlyObservableCollection<IDockable> Dockables { get; }

    public void Dispose()
    {
        this.changeSubscription?.Dispose();
        GC.SuppressFinalize(this);
    }

    [RelayCommand]
    private void ShowDockable(IDockable dockable)
    {
        var dock = dockable.Owner;
        Debug.Assert(dock != null, "a minimized dock in the tray should always have a valid owner dock");

        this.docker.PinDock(dock);
    }

    private void UpdateDockables()
    {
        Debug.Assert(
            this.docks is not null,
            $"expecting docks to be not null when {nameof(this.UpdateDockables)} is called");

        // Create a new list of IDockable objects in the order they appear in the docks collection
        var orderedDockables = this.docks.SelectMany(dock => dock.Dockables).ToList();

        // Update the dockables collection to match the ordered list
        for (var i = 0; i < orderedDockables.Count; i++)
        {
            if (this.dockables.Count > i)
            {
                if (!this.dockables[i].Equals(orderedDockables[i]))
                {
                    this.dockables.Insert(i, orderedDockables[i]);
                }
            }
            else
            {
                this.dockables.Add(orderedDockables[i]);
            }
        }

        // Remove any extra items from the dockables collection
        while (this.dockables.Count > orderedDockables.Count)
        {
            this.dockables.RemoveAt(this.dockables.Count - 1);
        }
    }
}
