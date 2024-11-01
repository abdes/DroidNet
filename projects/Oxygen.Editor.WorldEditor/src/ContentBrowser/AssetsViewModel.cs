// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using Microsoft.Extensions.DependencyInjection;

/// <summary>
/// The ViewModel for the <see cref="AssetsView" /> view.
/// </summary>
[InjectAs(ServiceLifetime.Transient)]
public partial class AssetsViewModel : AbstractOutletContainer
{
    public AssetsViewModel(ViewModelToView vmToViewConverter)
    {
        this.VmToViewConverter = vmToViewConverter;
        this.Outlets.Add("right", (nameof(this.LayoutViewModel), null));
    }

    public object? LayoutViewModel => this.Outlets["right"].viewModel;

    public ViewModelToView VmToViewConverter { get; init; }
}
