// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Views;

using System.Diagnostics;
using System.Globalization;
using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Storage;
using WinRT;

/// <summary>
/// An empty page that can be used on its own or navigated to within a
/// Frame.
/// </summary>
[ViewModel(typeof(OpenProjectViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class OpenProjectView
{
    public OpenProjectView() => this.InitializeComponent();

    private void KnownLocationButton_OnClick(object? sender, KnownLocation e)
    {
        _ = sender;

        this.ViewModel!.SelectLocation(e);
    }

    private async void ListView_OnItemClickAsync(object sender, ItemClickEventArgs e)
    {
        _ = sender;

        var item = e.ClickedItem.As<IStorageItem>();
        Debug.WriteLine($"Item {item.Location} clicked", StringComparer.Ordinal);

        if (item is IFolder)
        {
            this.ViewModel!.ChangeFolder(item.Location);
        }
        else if (item.Name.EndsWith(".oxy", ignoreCase: true, CultureInfo.InvariantCulture))
        {
            await this.ViewModel!.OpenProjectFile((item as INestedItem)!.ParentPath).ConfigureAwait(false);
        }
    }

    private void FilterBox_OnTextChanged(object sender, TextChangedEventArgs args)
    {
        _ = sender;
        _ = args;

        this.ViewModel!.ApplyFilter(this.FilterBox.Text);
    }

    private async void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        await this.ViewModel!.Initialize().ConfigureAwait(true);
    }
}

/// <summary>
/// A <see cref="DataTemplateSelector" /> that can return the correct template to be used based on whether the item being displayed
/// is an <see cref="IFolder" /> or an <see cref="IDocument" />.
/// </summary>
internal sealed partial class FileListDataTemplateSelector : DataTemplateSelector
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
