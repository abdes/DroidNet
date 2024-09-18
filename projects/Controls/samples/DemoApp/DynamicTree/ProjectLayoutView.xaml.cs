// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.Demo.Model;
using DroidNet.Controls.DynamicTree;
using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;

/// <summary>
/// A View that shows a hierarchical layout of a <see cref="Project">project</see> that has <see cref="Scene">scenes</see>, which
/// in turn can hold multiple <see cref="Entity">entities</see>. It demonstrates the flexibility of the <see cref="DynamicTreeControl" />
/// in representing hierarchical layouts of mixed types which can be loaded dynamically.
/// </summary>
[ViewModel(typeof(ProjectLayoutViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class ProjectLayoutView
{
    public ProjectLayoutView() => this.InitializeComponent();

    private async void ProjectLayoutView_OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        if (this.ViewModel is not null)
        {
            await this.ViewModel.LoadProjectCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        }
    }
}
