// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.TreeView;
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
    private async Task LoadProject()
    {
        this.Project = await ProjectLoaderService.LoadProjectAsync("Sample Project").ConfigureAwait(true);

        this.Root = new ProjectAdapter(this.Project)
        {
            Level = -1,
        };
    }
}
