// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace Oxygen.Editor.WorldEditor.Editors;

[ViewModel(typeof(DocumentHostViewModel))]
public sealed partial class TabbedDocumentView : UserControl
{
    private const double DragThreshold = 20.0; // pixels
                                               // Time (ms) to keep a pending tear-out window before closing it if unused
    private const int PendingWindowTimeoutMs = 3000; // Time (ms) to keep a pending tear-out window before closing it if unused

    // Drag state
    private bool isPointerPressed;
    private Point pointerPressedPoint;
    private TabbedDocumentItem? pointerPressedDoc;
    private uint? capturedPointerId;

    // Map a TabView to a newly-created Window during a tear-out lifecycle (with creation time)
    private readonly Dictionary<TabView, (Window Window, DateTime Created)> pendingTearOutWindows = new();

    public TabbedDocumentView()
    {
        this.InitializeComponent();
        this.Loaded += this.TabbedDocumentView_Loaded;
        this.Unloaded += this.TabbedDocumentView_Unloaded;
    }

    private void TabbedDocumentView_Loaded(object sender, RoutedEventArgs e)
    {
        if (this.ViewModel is null)
        {
            Debug.WriteLine("TabbedDocumentView.Loaded: ViewModel is null");
            return;
        }

        Debug.WriteLine("TabbedDocumentView.Loaded: initializing and populating tabs");
        this.PopulateTabs();

        // subscribe to collection changes to keep tabs in sync (use named handler so we can unsubscribe)
        this.ViewModel.TabbedDocuments.CollectionChanged -= this.TabbedDocuments_CollectionChanged;
        this.ViewModel.TabbedDocuments.CollectionChanged += this.TabbedDocuments_CollectionChanged;

        // Attach TabView-level handlers (moved from XAML to code-behind so wiring is centralized)
        var tabView = this.GetTabView();
        if (tabView is not null)
        {
            // detach first to be idempotent
            tabView.TabCloseRequested -= this.Tabs_OnTabCloseRequested;
            tabView.AddTabButtonClick -= this.Tabs_OnAddTabButtonClick;
            tabView.ExternalTornOutTabsDropping -= this.Tabs_OnExternalTornOutTabsDropping;
            tabView.ExternalTornOutTabsDropped -= this.Tabs_OnExternalTornOutTabsDropped;
            tabView.TabTearOutRequested -= this.Tabs_OnTabTearOutRequested;
            tabView.TabTearOutWindowRequested -= this.Tabs_OnTabTearOutWindowRequested;

            tabView.TabCloseRequested += this.Tabs_OnTabCloseRequested;
            tabView.AddTabButtonClick += this.Tabs_OnAddTabButtonClick;
            tabView.ExternalTornOutTabsDropping += this.Tabs_OnExternalTornOutTabsDropping;
            tabView.ExternalTornOutTabsDropped += this.Tabs_OnExternalTornOutTabsDropped;
            tabView.TabTearOutRequested += this.Tabs_OnTabTearOutRequested;
            tabView.TabTearOutWindowRequested += this.Tabs_OnTabTearOutWindowRequested;
            Debug.WriteLine("TabbedDocumentView.Loaded: TabView handlers attached");
        }

        // Note: the view handles tear-out/window creation itself; we don't create windows from the VM event here.
    }

    private void TabbedDocumentView_Unloaded(object sender, RoutedEventArgs e)
    {
        Debug.WriteLine("TabbedDocumentView.Unloaded: detaching handlers");

        // Detach handlers wired in Loaded to avoid leaks and duplicate wiring when reloaded
        var tabView = this.GetTabView();
        if (tabView is not null)
        {
            tabView.TabCloseRequested -= this.Tabs_OnTabCloseRequested;
            tabView.AddTabButtonClick -= this.Tabs_OnAddTabButtonClick;
            tabView.ExternalTornOutTabsDropping -= this.Tabs_OnExternalTornOutTabsDropping;
            tabView.ExternalTornOutTabsDropped -= this.Tabs_OnExternalTornOutTabsDropped;
            tabView.TabTearOutRequested -= this.Tabs_OnTabTearOutRequested;
            tabView.TabTearOutWindowRequested -= this.Tabs_OnTabTearOutWindowRequested;

            // Also detach the handlers managed by PopulateTabs for safety
            tabView.TabDragStarting -= this.TabView_TabDragStarting;
        }

        if (this.ViewModel is not null)
        {
            Debug.WriteLine("TabbedDocumentView.Unloaded: detaching ViewModel collection changed handler");
            this.ViewModel.TabbedDocuments.CollectionChanged -= this.TabbedDocuments_CollectionChanged;
        }
    }

    private void TabbedDocuments_CollectionChanged(object? sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs e)
    {
        Debug.WriteLine($"TabbedDocumentView.TabbedDocuments_CollectionChanged: Action={e?.Action}");
        this.PopulateTabs();
    }

    private TabView? GetTabView()
    {
        return this.FindName("PART_TabView") as TabView;
    }

    private void PopulateTabs()
    {
        var tabView = this.GetTabView();
        if (tabView is null)
        {
            Debug.WriteLine("TabbedDocumentView.PopulateTabs: TabView is null");
            return;
        }

        if (this.ViewModel is null)
        {
            Debug.WriteLine("TabbedDocumentView.PopulateTabs: ViewModel is null");
            return;
        }

        // Ensure handlers are attached once
        tabView.TabDragStarting -= this.TabView_TabDragStarting;
        tabView.TabDragStarting += this.TabView_TabDragStarting;
        Debug.WriteLine("TabbedDocumentView.PopulateTabs: TabDragStarting handler attached/ensured");

        // Use the control's public Items collection directly (no reflection)
        var items = tabView.TabItems;
        items.Clear();
        Debug.WriteLine($"TabbedDocumentView.PopulateTabs: Clearing and repopulating {this.ViewModel.TabbedDocuments.Count} tabs");

        foreach (var doc in this.ViewModel.TabbedDocuments)
        {
            var item = new TabViewItem();

            // header
            var headerPanel = new StackPanel { Orientation = Orientation.Horizontal };
            var titleText = new TextBlock { Text = doc.Title, VerticalAlignment = VerticalAlignment.Center };
            headerPanel.Children.Add(titleText);
            var detach = new Button { Content = "↗", Tag = doc };
            detach.Click += this.Detach_Click;
            headerPanel.Children.Add(detach);

            // Note: do not attach legacy pointer-based drag handlers here.
            // The TabView's built-in tear-out events (TabTearOutRequested / TabDroppedOutside)
            // are used to handle drag-to-detach behavior. Attaching both caused duplicate
            // window creation when the built-in tear-out was used.
            item.Header = headerPanel;

            // Create a fresh content element for this host using factory
            try
            {
                item.Content = doc.CreateContent?.Invoke();
            }
            catch
            {
                // If view creation fails keep content null to avoid throwing during UI construction
                item.Content = null;
            }

            item.IsClosable = doc.IsClosable;

            // keep a reference to the underlying document
            item.DataContext = doc;
            item.Tag = doc;

            Debug.WriteLine($"TabbedDocumentView.PopulateTabs: Adding tab '{doc?.Title}' Closable={doc?.IsClosable}");
            items.Add(item);
        }
    }

    private void TabView_TabDragStarting(TabView sender, TabViewTabDragStartingEventArgs args)
    {
        Debug.WriteLine("TabbedDocumentView.TabView_TabDragStarting: Tab drag starting");

        // Store reference to dragged doc
        if (args.Tab is TabViewItem item)
        {
            if (item.DataContext is TabbedDocumentItem doc)
            {
                args.Tab.DataContext = doc; // store doc on tab for later retrieval
                Debug.WriteLine($"TabbedDocumentView.TabView_TabDragStarting: Stored DataContext doc '{doc.Title}' on args.Tab");
            }
        }
    }

    private void OnHeader_PointerPressed(object? sender, PointerRoutedEventArgs e, TabbedDocumentItem doc)
    {
        Debug.WriteLine($"TabbedDocumentView.OnHeader_PointerPressed: Doc='{doc?.Title}' PointerId={e.Pointer.PointerId}");

        // Start drag detection
        this.isPointerPressed = true;
        this.pointerPressedPoint = e.GetCurrentPoint(this).Position;
        this.pointerPressedDoc = doc;
        this.capturedPointerId = e.Pointer.PointerId;

        // Capture pointer to continue receiving events
        if (sender is UIElement ue)
        {
            ue.CapturePointer(e.Pointer);
            Debug.WriteLine($"TabbedDocumentView.OnHeader_PointerPressed: Captured pointer {e.Pointer.PointerId}");
        }
    }

    private void OnHeader_PointerMoved(object? sender, PointerRoutedEventArgs e, TabbedDocumentItem doc)
    {
        if (!this.isPointerPressed)
        {
            return;
        }

        if (this.pointerPressedDoc is null)
        {
            return;
        }

        if (this.capturedPointerId != e.Pointer.PointerId)
        {
            return;
        }

        var current = e.GetCurrentPoint(this).Position;
        var dx = current.X - this.pointerPressedPoint.X;
        var dy = current.Y - this.pointerPressedPoint.Y;
        var distSq = (dx * dx) + (dy * dy);
        Debug.WriteLine($"TabbedDocumentView.OnHeader_PointerMoved: dx={dx} dy={dy} distSq={distSq}");
        if (distSq >= DragThreshold * DragThreshold)
        {
            // perform detach
            Debug.WriteLine($"TabbedDocumentView.OnHeader_PointerMoved: Drag threshold exceeded; detaching doc '{this.pointerPressedDoc?.Title}'");
            this.TryDetachByDrag(this.pointerPressedDoc!);

            // release capture and reset state
            if (sender is UIElement ue)
            {
                ue.ReleasePointerCapture(e.Pointer);
                Debug.WriteLine($"TabbedDocumentView.OnHeader_PointerMoved: Released pointer capture {e.Pointer.PointerId}");
            }

            this.isPointerPressed = false;
            this.pointerPressedDoc = null;
            this.capturedPointerId = null;
        }
    }

    private void OnHeader_PointerReleased(object? sender, PointerRoutedEventArgs e, TabbedDocumentItem doc)
    {
        Debug.WriteLine($"TabbedDocumentView.OnHeader_PointerReleased: PointerId={e.Pointer.PointerId}");

        // end press without drag
        if (this.capturedPointerId == e.Pointer.PointerId)
        {
            if (sender is UIElement ue)
            {
                ue.ReleasePointerCapture(e.Pointer);
                Debug.WriteLine($"TabbedDocumentView.OnHeader_PointerReleased: Released pointer capture {e.Pointer.PointerId}");
            }
        }

        this.isPointerPressed = false;
        this.pointerPressedDoc = null;
        this.capturedPointerId = null;
    }

    private void OnHeader_PointerCanceled(object? sender, PointerRoutedEventArgs e, TabbedDocumentItem doc)
    {
        Debug.WriteLine($"TabbedDocumentView.OnHeader_PointerCanceled: PointerId={e.Pointer.PointerId}");

        // cancel
        if (this.capturedPointerId == e.Pointer.PointerId)
        {
            if (sender is UIElement ue)
            {
                ue.ReleasePointerCapture(e.Pointer);
                Debug.WriteLine($"TabbedDocumentView.OnHeader_PointerCanceled: Released pointer capture {e.Pointer.PointerId}");
            }
        }

        this.isPointerPressed = false;
        this.pointerPressedDoc = null;
        this.capturedPointerId = null;
    }

    private void TryDetachByDrag(TabbedDocumentItem doc)
    {
        Debug.WriteLine($"TabbedDocumentView.TryDetachByDrag: Attempting detach for doc '{doc?.Title}'");
        if (doc is null)
        {
            Debug.WriteLine("TabbedDocumentView.TryDetachByDrag: doc is null - aborting");
            return;
        }

        if (this.ViewModel is null)
        {
            Debug.WriteLine("TabbedDocumentView.TryDetachByDrag: ViewModel is null - aborting");
            return;
        }

        // Let the view handle the detach/tear-out and window creation instead of relying on VM events.
        Debug.WriteLine($"TabbedDocumentView.TryDetachByDrag: Creating window for '{doc.Title}'");
        this.CreateWindowForDocument(doc);
    }

    private void Detach_Click(object? sender, RoutedEventArgs e)
    {
        Debug.WriteLine("TabbedDocumentView.Detach_Click: Detach button clicked");
        if (sender is not Button btn)
        {
            Debug.WriteLine("TabbedDocumentView.Detach_Click: sender is not Button");
            return;
        }

        if (btn.Tag is not TabbedDocumentItem doc)
        {
            Debug.WriteLine("TabbedDocumentView.Detach_Click: Button.Tag is not TabbedDocumentItem");
            return;
        }

        if (this.ViewModel is null)
        {
            Debug.WriteLine("TabbedDocumentView.Detach_Click: ViewModel is null");
            return;
        }

        // Handle detach in the view: remove from current host and open in a new window.
        Debug.WriteLine($"TabbedDocumentView.Detach_Click: Creating window for '{doc.Title}'");
        this.CreateWindowForDocument(doc);
    }

    /// <summary>
    ///     Create a new Window for the given document and remove it from the current host.
    ///     This keeps window creation in the view (matching the WinUI Gallery sample behavior)
    ///     and avoids creating windows from VM events in multiple places.
    /// </summary>
    private void CreateWindowForDocument(TabbedDocumentItem doc)
    {
        Debug.WriteLine($"TabbedDocumentView.CreateWindowForDocument: Starting for doc '{doc?.Title}'");
        if (doc is null)
        {
            Debug.WriteLine("TabbedDocumentView.CreateWindowForDocument: doc is null - aborting");
            return;
        }

        // Remove from current VM without invoking VM detach command (so we don't trigger other listeners)
        if (this.ViewModel is not null)
        {
            var index = this.ViewModel.TabbedDocuments.IndexOf(doc);
            Debug.WriteLine($"TabbedDocumentView.CreateWindowForDocument: Found index {index} in current host");
            if (index >= 0)
            {
                // If the doc is the selected one, attempt to select previous similar to VM.DetachDocument
                if (ReferenceEquals(this.ViewModel.SelectedDocument, doc))
                {
                    Debug.WriteLine("TabbedDocumentView.CreateWindowForDocument: Doc is selected, adjusting SelectedDocumentIndex");
                    if (this.ViewModel.TabbedDocuments.Count == 1)
                    {
                        this.ViewModel.SelectedDocumentIndex = -1;
                    }
                    else
                    {
                        this.ViewModel.SelectedDocumentIndex = index > 0 ? index - 1 : 0;
                    }
                }

                this.ViewModel.TabbedDocuments.RemoveAt(index);
                Debug.WriteLine($"TabbedDocumentView.CreateWindowForDocument: Removed doc '{doc.Title}' from original host at index {index}");
            }
        }

        // Create a new host and attach the document to it
        var newHost = new DocumentHostViewModel();
        newHost.TabbedDocuments.Add(doc);
        Debug.WriteLine($"TabbedDocumentView.CreateWindowForDocument: Added doc '{doc.Title}' to new host (count={newHost.TabbedDocuments.Count})");

        var newPage = new TabbedDocumentView();
        newPage.ViewModel = newHost;

        var newWindow = new Window { Content = newPage };
        Debug.WriteLine("TabbedDocumentView.CreateWindowForDocument: Created new window for document; activation deferred to tear-out handler");
    }

    private void Tabs_OnTabCloseRequested(TabView sender, TabViewTabCloseRequestedEventArgs args)
    {
        Debug.WriteLine("TabbedDocumentView.Tabs_OnTabCloseRequested: Close requested");
        if (args.Item is not TabViewItem item)
        {
            Debug.WriteLine("TabbedDocumentView.Tabs_OnTabCloseRequested: args.Item is not TabViewItem");
            return;
        }

        if (item.Tag is not TabbedDocumentItem doc)
        {
            Debug.WriteLine("TabbedDocumentView.Tabs_OnTabCloseRequested: item.Tag is not TabbedDocumentItem");
            return;
        }

        if (this.ViewModel is null)
        {
            Debug.WriteLine("TabbedDocumentView.Tabs_OnTabCloseRequested: ViewModel is null");
            return;
        }

        if (this.ViewModel.CloseDocumentCommand is not null && this.ViewModel.CloseDocumentCommand.CanExecute(doc))
        {
            Debug.WriteLine($"TabbedDocumentView.Tabs_OnTabCloseRequested: Executing CloseDocumentCommand for '{doc.Title}'");
            this.ViewModel.CloseDocumentCommand.Execute(doc);
        }
    }

    private void Tabs_OnAddTabButtonClick(TabView sender, object args)
    {
        Debug.WriteLine("TabbedDocumentView.Tabs_OnAddTabButtonClick: Add tab button clicked");
        if (this.ViewModel is null)
        {
            Debug.WriteLine("TabbedDocumentView.Tabs_OnAddTabButtonClick: ViewModel is null");
            return;
        }

        if (this.ViewModel.AddNewDocumentCommand is not null && this.ViewModel.AddNewDocumentCommand.CanExecute(null))
        {
            Debug.WriteLine("TabbedDocumentView.Tabs_OnAddTabButtonClick: Executing AddNewDocumentCommand");
            this.ViewModel.AddNewDocumentCommand.Execute(null);
        }
    }

    private void Tabs_OnExternalTornOutTabsDropping(TabView sender, TabViewExternalTornOutTabsDroppingEventArgs args)
    {
        Debug.WriteLine($"TabbedDocumentView.Tabs_OnExternalTornOutTabsDropping: Allowing drop. DropIndex={args.DropIndex}");

        // Allow dropping torn-out tabs into this TabView
        args.AllowDrop = true;
    }

    private void Tabs_OnExternalTornOutTabsDropped(TabView sender, TabViewExternalTornOutTabsDroppedEventArgs args)
    {
        Debug.WriteLine($"TabbedDocumentView.Tabs_OnExternalTornOutTabsDropped: Dropped tabs count={args.Tabs.Cast<TabViewItem>().Count()} DropIndex={args.DropIndex}");
        var position = 0;

        foreach (var tab in args.Tabs.Cast<TabViewItem>())
        {
            // Remove from the original parent TabView if present
            var parent = this.GetParentTabView(tab);

            Debug.WriteLine($"TabbedDocumentView.Tabs_OnExternalTornOutTabsDropped: Processing tab Tag='{tab.Tag}' ParentTabView={(parent is null ? "null" : "found")}");

            if (parent is not null)
            {
                parent.TabItems.Remove(tab);
                Debug.WriteLine("TabbedDocumentView.Tabs_OnExternalTornOutTabsDropped: Removed tab from original parent");
            }

            // Insert into this TabView at the drop index
            sender.TabItems.Insert(args.DropIndex + position, tab);
            Debug.WriteLine($"TabbedDocumentView.Tabs_OnExternalTornOutTabsDropped: Inserted tab at {args.DropIndex + position}");
            position++;
        }
    }

    private void Tabs_OnTabTearOutRequested(TabView sender, TabViewTabTearOutRequestedEventArgs args)
    {
        // Create a new window and move the tabs into it. The Gallery sample creates a new page instance
        // and uses helper methods; here we create a minimal window with a TabbedDocumentView and move the items.
        if (args is null)
        {
            Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutRequested: args is null - aborting");
            return;
        }

        Debug.WriteLine($"TabbedDocumentView.Tabs_OnTabTearOutRequested: Tabs count={args.Tabs.Cast<TabViewItem>().Count()}");

        // If a window was already created by Tabs_OnTabTearOutWindowRequested for this TabView, reuse it
        if (this.pendingTearOutWindows.TryGetValue(sender, out var pending))
        {
            var existingWindow = pending.Window;
            Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutRequested: Found pending window created by TabTearOutWindowRequested - reusing it");

            var existingPage = existingWindow.Content as TabbedDocumentView;
            foreach (var tab in args.Tabs.Cast<TabViewItem>())
            {
                var parent = this.GetParentTabView(tab);
                Debug.WriteLine($"TabbedDocumentView.Tabs_OnTabTearOutRequested: Moving tab Tag='{tab.Tag}' Parent={(parent is null ? "null" : "found")}");
                parent?.TabItems.Remove(tab);
                existingPage?.GetTabView()?.TabItems.Add(tab);
            }

            // Activate the window now that we've moved content into it
            try
            {
                existingWindow.Activate();
                Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutRequested: Activated pending window after moving tabs");
            }
            catch
            {
                Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutRequested: Failed to activate pending window");
            }

            // We've handled the move into the already-created window; remove the pending marker
            this.pendingTearOutWindows.Remove(sender);
            Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutRequested: Tabs moved into existing window; skipping new window creation");
            return;
        }

        // Fallback - no pending window, create a new one
        var newPage = new TabbedDocumentView();
        var newWindow = new Window { Content = newPage };

        // Move each tab into the new page
        foreach (var tab in args.Tabs.Cast<TabViewItem>())
        {
            var parent = this.GetParentTabView(tab);
            Debug.WriteLine($"TabbedDocumentView.Tabs_OnTabTearOutRequested: Moving tab Tag='{tab.Tag}' Parent={(parent is null ? "null" : "found")}");
            parent?.TabItems.Remove(tab);
            newPage.GetTabView()?.TabItems.Add(tab);
        }

        newWindow.Activate();
        Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutRequested: New window activated for torn-out tabs");
    }

    private void Tabs_OnTabTearOutWindowRequested(TabView sender, TabViewTabTearOutWindowRequestedEventArgs args)
    {
        // Create a window and assign its AppWindow Id back to the args so the TabView can use it
        var newPage = new TabbedDocumentView();
        var newWindow = new Window { Content = newPage };

        // Try to set the NewWindowId if available on the AppWindow
        try
        {
            args.NewWindowId = newWindow.AppWindow.Id;
            Debug.WriteLine($"TabbedDocumentView.Tabs_OnTabTearOutWindowRequested: Set NewWindowId={args.NewWindowId}");
        }
        catch
        {
            // Some hosts may not support AppWindow; just activate the window.
            Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutWindowRequested: AppWindow Id not available on this host");
        }

        // Store the newly-created window so the other tear-out handler can reuse it and avoid creating a second.
        // Do NOT activate it yet; activation will happen when tabs are moved into it. This avoids showing a window for simple clicks.
        try
        {
            lock (this.pendingTearOutWindows)
            {
                this.pendingTearOutWindows[sender] = (newWindow, DateTime.UtcNow);
            }

            Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutWindowRequested: Stored pending tear-out window for TabView (not activated)");

            // Schedule cleanup: if the pending window isn't used within 2 seconds, close it and remove the pending marker
            _ = Task.Run(async () =>
            {
                await Task.Delay(PendingWindowTimeoutMs).ConfigureAwait(false);
                try
                {
                    var remove = false;
                    lock (this.pendingTearOutWindows)
                    {
                        if (this.pendingTearOutWindows.TryGetValue(sender, out var info) && info.Window == newWindow)
                        {
                            // still pending and matches our window -> remove and close
                            remove = true;
                            this.pendingTearOutWindows.Remove(sender);
                        }
                    }

                    if (remove)
                    {
                        try
                        {
                            // Must close on the window's UI thread to avoid COM/WinRT errors
                            var dq = newWindow.DispatcherQueue;
                            if (dq is not null)
                            {
                                dq.TryEnqueue(() =>
                                {
                                    try
                                    {
                                        newWindow.Close();
                                        Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutWindowRequested: Pending window expired and was closed (UI thread)");
                                    }
                                    catch
                                    {
                                        Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutWindowRequested: Failed to close expired pending window (UI thread)");
                                    }
                                });
                            }
                            else
                            {
                                // Fallback: attempt to close directly
                                newWindow.Close();
                                Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutWindowRequested: Pending window expired and was closed (direct)");
                            }
                        }
                        catch
                        {
                            Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutWindowRequested: Failed to close expired pending window");
                        }
                    }
                }
                catch
                {
                    // swallow - cleanup best-effort
                }
            });
        }
        catch
        {
            Debug.WriteLine("TabbedDocumentView.Tabs_OnTabTearOutWindowRequested: Failed to store pending window reference");
        }
    }

    private TabView? GetParentTabView(TabViewItem tab)
    {
        DependencyObject? current = tab;

        Debug.WriteLine($"TabbedDocumentView.GetParentTabView: Searching parent for tab Tag='{tab.Tag}'");
        while (current is not null)
        {
            if (current is TabView tv)
            {
                Debug.WriteLine("TabbedDocumentView.GetParentTabView: Found parent TabView");
                return tv;
            }

            current = VisualTreeHelper.GetParent(current);
        }

        Debug.WriteLine("TabbedDocumentView.GetParentTabView: No parent TabView found");
        return null;
    }
}
