// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.Shell;

using System.ComponentModel;
using System.Diagnostics;
using System.Windows.Input;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Windows.Foundation;
using WinUIEx;
using GridLength = Microsoft.UI.Xaml.GridLength;

/// <summary>The view for the application's main window shell.</summary>
[ViewModel(typeof(ShellViewModel))]
public sealed partial class ShellView : INotifyPropertyChanged
{
    private double minWindowWidth;

    public ShellView()
    {
        this.InitializeComponent();

        this.CustomTitleBar.Loaded += (_, _) => this.SetupCustomTitleBar();
        this.CustomTitleBar.SizeChanged += (_, _) => this.SetupCustomTitleBar();

        this.SettingsButton.Loaded
            += (_, _) => this.SettingsButton.Flyout = CreateMenuFlyout(this.ViewModel!.MenuItems);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public double MinWindowWidth
    {
        get => this.minWindowWidth;
        private set
        {
            if (Math.Abs(this.minWindowWidth - value) < 0.5f)
            {
                return;
            }

            this.minWindowWidth = value;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.MinWindowWidth)));

            if (this.ViewModel?.Window is WindowEx window && window.MinWidth < this.MinWindowWidth)
            {
                window.MinWidth = this.MinWindowWidth;
            }
        }
    }

    private static Windows.Graphics.RectInt32 GetRect(Rect bounds, double scale) => new(
        _X: (int)Math.Round(bounds.X * scale),
        _Y: (int)Math.Round(bounds.Y * scale),
        _Width: (int)Math.Round(bounds.Width * scale),
        _Height: (int)Math.Round(bounds.Height * scale));

    private static string GetFullId(string parentFullId, string id)
        => string.IsNullOrEmpty(parentFullId) ? id : $"{parentFullId}.{id}";

    private static MenuFlyout CreateMenuFlyout(
        IEnumerable<ShellViewModel.MenuItem> menuItems,
        ICommand? parentCommand = null,
        string parentFullId = "")
    {
        var menuFlyout = new MenuFlyout();

        foreach (var menuItem in menuItems)
        {
            if (menuItem.SubItems.Any())
            {
                var subMenuFlyoutItem = new MenuFlyoutSubItem
                {
                    Text = menuItem.Text,
                };

                // Clear and add sub-items
                subMenuFlyoutItem.Items.Clear();
                var subItems = CreateMenuFlyout(
                        menuItem.SubItems,
                        menuItem.Command ?? parentCommand,
                        GetFullId(parentFullId, menuItem.Id))
                    .Items;
                foreach (var subItem in subItems)
                {
                    subMenuFlyoutItem.Items.Add(subItem);
                }

                menuFlyout.Items.Add(subMenuFlyoutItem);
            }
            else
            {
                var flyoutItem = new MenuFlyoutItem
                {
                    Text = menuItem.Text,
                    Command = menuItem.Command ?? parentCommand,
                    CommandParameter = menuItem.Command == null && parentCommand != null
                        ? GetFullId(parentFullId, menuItem.Id)
                        : null,
                };

                var binding = new Binding
                {
                    Path = new PropertyPath("IsSelected"),
                    Source = menuItem,
                    Mode = BindingMode.OneWay,
                    Converter = new IsSelectedToIconConverter(),
                };
                flyoutItem.SetBinding(MenuFlyoutItem.IconProperty, binding);

                menuFlyout.Items.Add(flyoutItem);
            }
        }

        return menuFlyout;
    }

    private void SetupCustomTitleBar()
    {
        Debug.Assert(
            this.ViewModel?.Window is not null,
            "expecting a properly setup ViewModel when loaded");

        var appWindow = this.ViewModel.Window.AppWindow;
        var scaleAdjustment = this.CustomTitleBar.XamlRoot.RasterizationScale;

        //Debug.WriteLine($"SetRegionsForCustomTitleBar: {scaleAdjustment}");

        this.CustomTitleBar.Height = appWindow.TitleBar.Height / scaleAdjustment;

        this.LeftPaddingColumn.Width = new GridLength(appWindow.TitleBar.LeftInset / scaleAdjustment);
        this.RightPaddingColumn.Width = new GridLength(appWindow.TitleBar.RightInset / scaleAdjustment);

        //Debug.WriteLine($"SetRegionsForCustomTitleBar: padding left={this.LeftPaddingColumn.Width} , right={this.RightPaddingColumn.Width}");

        var transform = this.PrimaryCommands.TransformToVisual(visual: null);
        var bounds = transform.TransformBounds(
            new Rect(
                0,
                0,
                this.PrimaryCommands.ActualWidth,
                this.PrimaryCommands.ActualHeight));
        var primaryCommandsRect = GetRect(bounds, scaleAdjustment);

        //Debug.WriteLine($"SetRegionsForCustomTitleBar: primary width={primaryCommandsRect.Width} , height={primaryCommandsRect.Height}");

        transform = this.SecondaryCommands.TransformToVisual(visual: null);
        bounds = transform.TransformBounds(
            new Rect(
                0,
                0,
                this.SecondaryCommands.ActualWidth,
                this.SecondaryCommands.ActualHeight));
        var secondaryCommandsRect = GetRect(bounds, scaleAdjustment);

        //Debug.WriteLine($"SetRegionsForCustomTitleBar: secondary width={secondaryCommandsRect.Width} , height={secondaryCommandsRect.Height}");

        var rectArray = new[] { primaryCommandsRect, secondaryCommandsRect };

        var nonClientInputSrc = InputNonClientPointerSource.GetForWindowId(appWindow.Id);
        nonClientInputSrc.SetRegionRects(NonClientRegionKind.Passthrough, rectArray);

        this.MinWindowWidth = this.LeftPaddingColumn.Width.Value + this.IconColumn.ActualWidth +
                              this.PrimaryCommands.ActualWidth + this.DragColumn.MinWidth +
                              this.SecondaryCommands.ActualWidth +
                              this.RightPaddingColumn.Width.Value;
    }
}

public partial class IsSelectedToIconConverter : IValueConverter
{
    public object? Convert(object value, Type targetType, object parameter, string language)
    {
        var isSelected = (bool)value;
        return isSelected ? new SymbolIcon(Symbol.Accept) : null;
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
