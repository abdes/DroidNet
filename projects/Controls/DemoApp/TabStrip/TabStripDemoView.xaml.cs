// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Aura.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.TabStrip;

/// <summary>
///     View for demonstrating the TabStrip control.
/// </summary>
[ViewModel(typeof(TabStripDemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Views that get injected with the ViewModel must be public")]
public sealed partial class TabStripDemoView : Page
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="TabStripDemoView"/> class.
    /// </summary>
    public TabStripDemoView()
    {
        this.InitializeComponent();
        this.Loaded += this.TabStripDemoView_Loaded;
    }

    /// <summary>
    /// Gets the available tab width policy names.
    /// </summary>
    public static IEnumerable<string> TabWidthPolicyNames => Enum.GetNames<TabWidthPolicy>();

    /// <summary>
    /// Gets the list of available icon symbols for selection.
    /// </summary>
    public static IEnumerable<Symbol> AvailableIconSymbols { get; } =
    [
        Symbol.Home,
        Symbol.Document,
        Symbol.Save,
        Symbol.OpenFile,
        Symbol.Folder,
        Symbol.Setting,
        Symbol.Add,
        Symbol.Remove,
        Symbol.Edit,
        Symbol.Delete,
        Symbol.Play,
        Symbol.Pause,
        Symbol.Stop,
        Symbol.Previous,
        Symbol.Next,
        Symbol.Up,
        Symbol.Accept,
        Symbol.Cancel,
        Symbol.Clear,
        Symbol.Help,
    ];

    private void TabStripDemoView_Loaded(object? sender, RoutedEventArgs e)
    {
        // Seed a few sample tabs (one pinned, a couple regular)
        this.DemoTabStrip.Items.Add(new TabItem { Header = "Home", IsPinned = true, IsClosable = false });
        this.DemoTabStrip.Items.Add(new TabItem { Header = "Editor", IsClosable = true });
        this.DemoTabStrip.Items.Add(new TabItem { Header = "Logs", IsClosable = true });

        // Handle tab close requests
        this.DemoTabStrip.TabCloseRequested += this.DemoTabStrip_TabCloseRequested;
    }

    private void DemoTabStrip_TabCloseRequested(object? sender, TabCloseRequestedEventArgs e)
    {
        _ = sender; // Unused

        // Remove the tab from the Items collection
        this.DemoTabStrip.Items.Remove(e.Item);
    }

    private void AddTab_Click(object? sender, RoutedEventArgs e)
    {
        if (this.ViewModel is null)
        {
            return;
        }

        var header = string.IsNullOrWhiteSpace(this.ViewModel.NewItemHeader)
            ? string.Format(CultureInfo.InvariantCulture, "Tab With A Header {0}", this.DemoTabStrip.Items.Count + 1)
            : this.ViewModel.NewItemHeader;

        var ti = new TabItem
        {
            Header = header,
            IsPinned = this.ViewModel.NewItemIsPinned,
            IsClosable = this.ViewModel.NewItemIsClosable,
            Icon = this.ViewModel.NewItemIconSymbol.HasValue
                ? new SymbolIconSource { Symbol = this.ViewModel.NewItemIconSymbol.Value }
                : null,
        };

        this.DemoTabStrip.Items.Add(ti);

        // Clear header input for convenience
        this.ViewModel.NewItemHeader = string.Empty;
        this.ViewModel.NewItemIconSymbol = null;
    }
}
