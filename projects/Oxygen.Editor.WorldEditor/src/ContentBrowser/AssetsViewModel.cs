// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using DroidNet.Mvvm.Converters;
using DroidNet.Routing;

/// <summary>
/// The ViewModel for the <see cref="AssetsView" /> view.
/// </summary>
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
