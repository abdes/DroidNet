// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Demo.Model;
using DroidNet.Controls.Demo.Services;
using DroidNet.Controls.DynamicTree;
using DroidNet.Hosting.Generators;
using Microsoft.Extensions.DependencyInjection;

/// <summary>
/// The ViewModel for the <see cref="ProjectLayoutView" /> view.
/// </summary>
[InjectAs(ServiceLifetime.Singleton)]
public partial class ProjectLayoutViewModel() : DynamicTreeViewModel(showRoot: false)
{
    public Project? Project { get; private set; }

    [RelayCommand]
    private async Task LoadProjectAsync()
    {
        this.Project = new Project("Sample Project");
        await ProjectLoaderService.LoadProjectAsync(this.Project).ConfigureAwait(false);

        var root = new ProjectAdapter(this.Project)
        {
            Level = -1,
        };
        await this.InitializeRootAsync(root).ConfigureAwait(false);
    }
}
