// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Windows.Foundation;

namespace DroidNet.Docking.Controls;

/// <summary>
/// A container control to hold the tabs for dockables in a dock panel.
/// </summary>
/// <remarks>
/// The <see cref="DockableTabsBar"/> class provides functionality to manage and display dockable
/// entities as tabs. It supports different visual states such as normal, compact, and collapsed
/// modes, and allows for dynamic layout adjustments based on available space. The layout mode is
/// automatically chosen based on the available space and the number of dockable entities, and does
/// not need to be explicitly set by the user.
/// </remarks>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <code><![CDATA[
/// <Page
///     x:Class="MyApp.MainPage"
///     xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
///     xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
///     xmlns:local="using:MyApp"
///     xmlns:controls="using:DroidNet.Docking.Controls">
///     <Grid>
///         <controls:DockableTabsBar
///             Dockables="{Binding Dockables}"
///             ActiveDockable="{Binding ActiveDockable}"
///             IconConverter="{StaticResource IconConverter}" />
///     </Grid>
/// </Page>
/// ]]></code>
/// </example>
[TemplatePart(Name = PartRootGridName, Type = typeof(Grid))]
[TemplatePart(Name = PartItemsRepeaterName, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = PartToasterButtonName, Type = typeof(Button))]
[TemplateVisualState(Name = StateNormal, GroupName = GroupLayoutStates)]
[TemplateVisualState(Name = StateIconOnly, GroupName = GroupLayoutStates)]
[TemplateVisualState(Name = StateCollapsed, GroupName = GroupLayoutStates)]
public sealed partial class DockableTabsBar : Control
{
    /// <summary>
    /// The name of the root grid part in the control template.
    /// </summary>
    private const string PartRootGridName = "PartRootGrid";

    /// <summary>
    /// The name of the items repeater part in the control template.
    /// </summary>
    private const string PartItemsRepeaterName = "PartItemsRepeater";

    /// <summary>
    /// The name of the toaster button part in the control template.
    /// </summary>
    private const string PartToasterButtonName = "PartToasterButton";

    /// <summary>
    /// The visual state name for the normal layout state.
    /// </summary>
    private const string StateNormal = "Normal";

    /// <summary>
    /// The visual state name for the icon-only layout state.
    /// </summary>
    private const string StateIconOnly = "IconOnly";

    /// <summary>
    /// The visual state name for the collapsed layout state.
    /// </summary>
    private const string StateCollapsed = "Collapsed";

    /// <summary>
    /// The name of the group that contains the layout states.
    /// </summary>
    private const string GroupLayoutStates = "LayoutStates";

    private ItemsRepeater? itemsRepeater;
    private Button? toasterButton;

    /// <summary>
    /// Initializes a new instance of the <see cref="DockableTabsBar"/> class.
    /// </summary>
    public DockableTabsBar()
    {
        this.DefaultStyleKey = typeof(DockableTabsBar);
    }

    /// <inheritdoc/>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.SetupItemsRepeaterPart();
        this.SetupToasterButtonPart();

        this.UpdateSelectionStates();
    }

    /// <inheritdoc/>
    protected override Size MeasureOverride(Size availableSize)
    {
        this.AdjustLayout(availableSize);
        return base.MeasureOverride(availableSize);
    }

    private void SetupItemsRepeaterPart()
    {
        if (this.itemsRepeater is not null)
        {
            this.itemsRepeater.ElementPrepared -= this.OnElementPrepared;
        }

        this.itemsRepeater = this.GetTemplateChild(PartItemsRepeaterName) as ItemsRepeater
            ?? throw new InvalidOperationException($"{nameof(DockableTabsBar)} control requires an {nameof(ItemsRepeater)} part named `{PartItemsRepeaterName}`");

        this.itemsRepeater.ElementPrepared += this.OnElementPrepared;
        this.itemsRepeater.ItemsSource = this.Dockables;
    }

    private void SetupToasterButtonPart() => this.toasterButton = this.GetTemplateChild(PartToasterButtonName) as Button;

    private void OnElementPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        if (args.Element is DockableTab tab)
        {
            tab.TabActivated += this.OnTabClicked;
            tab.IsSelected = tab.Dockable == this.ActiveDockable;
            tab.IconConverter = this.IconConverter;
        }
    }

    private void OnIconConverterChanged(IValueConverter newValue)
    {
        if (this.itemsRepeater?.ItemsSource is null)
        {
            return;
        }

        var count = ((ReadOnlyObservableCollection<IDockable>)this.itemsRepeater.ItemsSource).Count;
        for (var i = 0; i < count; i++)
        {
            var element = this.itemsRepeater.TryGetElement(i);
            if (element is DockableTab tab)
            {
                tab.IconConverter = newValue;
            }
        }
    }

    private void OnTabClicked(object? sender, TabActivatedEventArgs e)
    {
        if (sender is DockableTab tab)
        {
            this.ActiveDockable = tab.Dockable;
        }
    }

    private void UpdateSelectionStates()
    {
        if (this.itemsRepeater?.ItemsSource is null)
        {
            return;
        }

        var count = ((ReadOnlyObservableCollection<IDockable>)this.itemsRepeater.ItemsSource).Count;
        for (var i = 0; i < count; i++)
        {
            var element = this.itemsRepeater.TryGetElement(i);
            if (element is DockableTab tab)
            {
                tab.IsSelected = tab.Dockable == this.ActiveDockable;
            }
        }
    }

    private void PopulateCollapsedModeFlyout()
    {
        if (this.toasterButton is null)
        {
            return;
        }

        var flyout = (MenuFlyout)this.toasterButton.Flyout;
        flyout.Items.Clear();

        foreach (var dockable in this.Dockables)
        {
            var menuItem = new MenuFlyoutItem
            {
                Text = dockable.Title,
                Command = new RelayCommand(() => this.ActiveDockable = dockable),
            };
            flyout.Items.Add(menuItem);
        }
    }

    /// <summary>
    /// Adjusts the layout of the dockable tabs based on the available size.
    /// </summary>
    /// <param name="availableSize">The available size for the layout.</param>
    /// <remarks>
    /// This method dynamically adjusts the layout of the dockable tabs based on the available width.
    /// It determines whether the tabs should be displayed in normal, icon-only, or collapsed mode.
    /// The layout mode is chosen based on the available width and the number of dockable entities.
    /// </remarks>
    private void AdjustLayout(Size availableSize)
    {
        if (this.itemsRepeater?.ItemsSource is null)
        {
            return;
        }

        // Create a new tabs layout using our current compact style state, and store the new
        // state after the layout does its first pass.
        var layout = new TabsLayout(this.itemsRepeater, availableSize) { IsCompact = this.IsCompact };
        this.IsCompact = layout.InitializeTabWidths();

        if (layout.ShouldCollapse)
        {
            this.PopulateCollapsedModeFlyout();
            _ = VisualStateManager.GoToState(this, StateCollapsed, useTransitions: true);
            return;
        }

        if (layout.IsCompact)
        {
            _ = VisualStateManager.GoToState(this, StateIconOnly, useTransitions: true);
            return;
        }

        layout.DistributeUnusedSpace();
        _ = VisualStateManager.GoToState(this, StateNormal, useTransitions: true);
    }

    /// <summary>
    /// Manages the layout of dockable tabs within the <see cref="DockableTabsBar"/> control.
    /// </summary>
    /// <remarks>
    /// The <see cref="TabsLayout"/> class is responsible for calculating and adjusting the widths of dockable tabs
    /// based on the available space. It determines whether the tabs should be displayed in normal, icon-only, or collapsed mode.
    /// </remarks>
    private sealed class TabsLayout
    {
        /// <summary>
        /// The minimum width for a tab.
        /// </summary>
        public const double MinTabWidth = 30;

        private readonly ItemsRepeater itemsRepeater;
        private readonly int itemCount;
        private readonly double availableWidth;
        private readonly double maxTabWidth;

        private readonly Dictionary<int, ItemMeasurementData> itemMeasurements = [];
        private double totalDesiredWidth;
        private double totalMinWidth;

        /// <summary>
        /// Initializes a new instance of the <see cref="TabsLayout"/> class.
        /// </summary>
        /// <param name="itemsRepeater">The <see cref="ItemsRepeater"/> that contains the dockable tabs.</param>
        /// <param name="availableSize">The available size for the layout.</param>
        public TabsLayout(ItemsRepeater itemsRepeater, Size availableSize)
        {
            this.itemsRepeater = itemsRepeater;
            this.availableWidth = availableSize.Width;
            this.itemCount = ((ReadOnlyObservableCollection<IDockable>)itemsRepeater.ItemsSource).Count;
            this.maxTabWidth = availableSize.Width / this.itemCount;
        }

        /// <summary>
        /// Gets or sets a value indicating whether the tabs are in compact mode. This property must
        /// be initialized with the current layout mode of the <see cref="DockableTabsBar"/>
        /// control, and will be updated by the <see cref="TabsLayout"/> after finishing its
        /// <see cref="InitializeTabWidths">initial pass</see>.
        /// </summary>
        public required bool IsCompact { get; set; }

        /// <summary>
        /// Gets a value indicating whether the tabs should be collapsed.
        /// </summary>
        public bool ShouldCollapse => this.totalMinWidth > this.availableWidth;

        /// <summary>
        /// Initializes the widths of the dockable tabs based on the available space and the desired widths of the tabs.
        /// </summary>
        /// <remarks>
        /// This method determines, based on the available space and the number of tabs, whether the
        /// tabs bar should be collapsed, compact or normal, and calculates the initial widths of
        /// the dockable tabs, using their desired width and the the maxim width they can fairly
        /// occupy in the bar. The calculated widths and other measurement data are stored for
        /// further layout adjustments in the <see cref="DistributeUnusedSpace">second pass</see>.
        /// </remarks>
        /// <returns><see langword="true"/> if the tabs are in compact mode; otherwise, <see langword="false"/>.</returns>
        public bool InitializeTabWidths()
        {
            this.IsCompact = this.maxTabWidth < MinTabWidth * 2;

            for (var i = 0; i < this.itemCount; i++)
            {
                var element = this.itemsRepeater.TryGetElement(i);
                if (element is DockableTab tab)
                {
                    tab.IsCompact = this.IsCompact;
                    var desiredWidth = tab.MeasureDesiredWidth();
                    var initialWidth = tab.IsCompact ? MinTabWidth : Math.Min(this.maxTabWidth, desiredWidth);
                    tab.Width = initialWidth;
                    tab.TextTrimming = initialWidth < desiredWidth ? TextTrimming.CharacterEllipsis : TextTrimming.None;

                    this.itemMeasurements[i] = new ItemMeasurementData
                    {
                        DesiredWidth = desiredWidth,
                        CurrentWidth = initialWidth,
                    };

                    this.totalDesiredWidth += initialWidth;
                    this.totalMinWidth += TabsLayout.MinTabWidth;
                }
            }

            return this.IsCompact;
        }

        /// <summary>
        /// Distributes any unused space among the tabs that need it.
        /// </summary>
        /// <remarks>
        /// The method will iterate until no tab needs extra space or all unused space has been
        /// allocated. A tab will not get allocated extra space if its current width is already
        /// equal to its desired width, and a tab which width even after getting extra space is less
        /// than its desired width will be setup to use ellipsis for its tab title.
        /// </remarks>
        public void DistributeUnusedSpace()
        {
            var unusedSpace = this.availableWidth - this.totalDesiredWidth;
            while (unusedSpace > 0)
            {
                var distributed = false;
                for (var i = 0; i < this.itemCount; i++)
                {
                    var element = this.itemsRepeater.TryGetElement(i);
                    if (element is DockableTab tab)
                    {
                        var itemData = this.itemMeasurements[i];
                        if (itemData.CurrentWidth < itemData.DesiredWidth)
                        {
                            var additionalWidth = Math.Min(unusedSpace, itemData.DesiredWidth - itemData.CurrentWidth);
                            itemData.CurrentWidth += additionalWidth;
                            tab.Width = itemData.CurrentWidth;
                            tab.TextTrimming = itemData.CurrentWidth < itemData.DesiredWidth ? TextTrimming.CharacterEllipsis : TextTrimming.None;
                            unusedSpace -= additionalWidth;
                            distributed = true;
                        }
                    }
                }

                if (!distributed)
                {
                    break;
                }
            }
        }

        /// <summary>
        /// Stores measurement data for a tab item.
        /// </summary>
        private sealed class ItemMeasurementData
        {
            /// <summary>
            /// Gets or sets the desired width of the tab item.
            /// </summary>
            public double DesiredWidth { get; set; }

            /// <summary>
            /// Gets or sets the current width of the tab item.
            /// </summary>
            public double CurrentWidth { get; set; }
        }
    }
}
