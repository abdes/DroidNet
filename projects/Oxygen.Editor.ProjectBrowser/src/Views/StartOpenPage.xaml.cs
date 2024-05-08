// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Views;

using System.Diagnostics;
using System.Globalization;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Storage;
using WinRT;

/// <summary>
/// An empty page that can be used on its own or navigated to within a
/// Frame.
/// </summary>
public sealed partial class StartOpenPage
{
    /// <summary>Initializes a new instance of the <see cref="StartOpenPage" /> class.</summary>
    public StartOpenPage()
    {
        this.InitializeComponent();

        this.ViewModel = Ioc.Default.GetRequiredService<StartOpenViewModel>();
    }

    public StartOpenViewModel ViewModel { get; }

    protected override async void OnNavigatedTo(NavigationEventArgs e)
    {
        Debug.WriteLine("Navigation to StartOpenPage.");

        await this.ViewModel.Initialize().ConfigureAwait(true);
    }

    private void KnownLocationButton_OnClick(object? sender, KnownLocation e)
    {
        _ = sender;

        this.ViewModel.SelectLocation(e);
    }

    private void ListView_OnItemClick(object sender, ItemClickEventArgs e)
    {
        _ = sender;

        var item = e.ClickedItem.As<IStorageItem>();
        Debug.WriteLine($"Item {item.Location} clicked", StringComparer.Ordinal);

        if (item is IFolder)
        {
            this.ViewModel.ChangeFolder(item.Location);
        }
        else if (item.Name.EndsWith(".oxy", ignoreCase: true, CultureInfo.InvariantCulture))
        {
            this.ViewModel.OpenProjectFile((item as INestedItem)!.ParentPath);
        }
    }

    private void FilterBox_OnTextChanged(object sender, TextChangedEventArgs args)
    {
        _ = sender;
        _ = args;

        this.ViewModel.ApplyFilter(this.FilterBox.Text);
    }
}

internal sealed class FileListDataTemplateSelector : DataTemplateSelector
{
    // Initialized as static resource in the XAML
#pragma warning disable CS8618
    public DataTemplate FolderTemplate { get; set; }
#pragma warning restore CS8618

    // Initialized as static resource in the XAML
#pragma warning disable CS8618
    public DataTemplate FileTemplate { get; set; }
#pragma warning restore CS8618

    protected override DataTemplate SelectTemplateCore(object item)
        => this.SelectTemplateCore(item, container: null);

    protected override DataTemplate SelectTemplateCore(object item, DependencyObject? container)
        => item is IFolder ? this.FolderTemplate : this.FileTemplate;
}
