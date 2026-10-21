// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Reflection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.ProjectBrowser.Projects;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// Represents a ListView control that displays known locations grouped by categories.
/// </summary>
/// <remarks>
/// The known locations are grouped into categories such as "Recent Projects", "User Folders", and "This Computer".
/// The groups are presented in the following order: <em>Recent Projects</em>, <em>User Folders</em>, and <em>This Computer</em>.
/// Within each group, the locations are sorted alphabetically by their name.
/// The groups are separated with a separator in place of the group header, except for the first group.
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <code><![CDATA[
/// <pbc:KnownLocationsListView
///     ItemsSource="{x:Bind ViewModel.KnownLocations, Mode=OneWay}"
///     SelectedItem="{x:Bind ViewModel.SelectedLocation, Mode=TwoWay}" />
/// ]]></code>
/// </example>
public partial class KnownLocationsListView : ListView
{
    private readonly CollectionViewSource groupedLocationsViewSource;

    /// <summary>
    /// Initializes a new instance of the <see cref="KnownLocationsListView"/> class.
    /// </summary>
    public KnownLocationsListView()
    {
        this.DefaultStyleKey = typeof(KnownLocationsListView);

        // Load the resource dictionary
        var assemblyName = Assembly.GetExecutingAssembly().GetName().Name;
        var resourceDictionary = new ResourceDictionary
        {
            Source = new Uri($"ms-appx:///{assemblyName}/Controls/KnownLocationsListView.xaml"),
        };
        this.Resources.MergedDictionaries.Add(resourceDictionary);
        this.groupedLocationsViewSource = (CollectionViewSource)this.Resources["GroupedLocationsSource"];

        // Set GroupStyle
        var groupStyle = new GroupStyle
        {
            HeaderTemplate = (DataTemplate)this.Resources["KnownLocationsHeaderTemplate"],
        };
        this.GroupStyle.Add(groupStyle);
        this.ItemsPanel = (ItemsPanelTemplate)this.Resources["KnownLocationsItemsPanelTemplate"];

        this.Loaded += (_, _) =>
        {
            this.InitializeKnownLocationsCollectionViewSource();
            this.ItemsSource = this.groupedLocationsViewSource.View;
        };
    }

    /// <summary>
    /// Initializes the collection view source for known locations, grouping them by categories.
    /// </summary>
    /// <exception cref="InvalidOperationException">Thrown when the ItemsSource is not set to an enumerable type.</exception>
    private void InitializeKnownLocationsCollectionViewSource()
    {
        var knownnLocations = this.ItemsSource as IEnumerable<KnownLocation> ?? throw new InvalidOperationException($"you must set the ItemsSource of {nameof(KnownLocationsListView)} with an enumarable type");
        var groupedLocations = knownnLocations
            .OrderBy(l => l.Name, StringComparer.Ordinal)
            .Select(loc => (Location: loc, Group: loc.Key switch
            {
                KnownLocations.RecentProjects => "Recent Projects",
                KnownLocations.Desktop => "User Folders",
                KnownLocations.Documents => "User Folders",
                KnownLocations.Downloads => "User Folders",
                KnownLocations.OneDrive => "User Folders",
                KnownLocations.LocalProjects => "Local Storage",
                KnownLocations.ThisComputer => "This Computer",
                _ => throw new System.ComponentModel.InvalidEnumArgumentException(nameof(loc), (int)loc.Key, typeof(KnownLocations)),
            }))
            .ToList();

        var source = groupedLocations
            .GroupBy(item => item.Group, StringComparer.Ordinal)
            .Select(group => new KnownLocationsGroup(group.Select(item => item.Location))
            {
                Order = group.Key switch
                {
                    "Recent Projects" => 0,
                    "User Folders" => 1,
                    "Local Storage" => 2,
                    "This Computer" => 3,
                    _ => throw new ArgumentException($"unexpected known locations group key: {group.Key}", nameof(group)),
                },
                Key = group.Key,
            })
            .OrderBy(g => g.Order);

        this.groupedLocationsViewSource.Source = new ObservableCollection<KnownLocationsGroup>(source);
    }
}
