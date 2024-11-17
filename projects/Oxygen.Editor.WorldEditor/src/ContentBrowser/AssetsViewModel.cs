// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Converters;
using DroidNet.Routing.WinUI;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// The ViewModel for the <see cref="AssetsView" /> view.
/// </summary>
public partial class AssetsViewModel : AbstractOutletContainer
{
    /// <summary>
    /// Initializes a new instance of the <see cref="AssetsViewModel"/> class.
    /// </summary>
    /// <param name="vmToViewConverter"></param>
    public AssetsViewModel(ViewModelToView vmToViewConverter)
    {
        this.VmToViewConverter = vmToViewConverter;
        this.Outlets.Add("right", (nameof(this.LayoutViewModel), null));
    }

    public object? LayoutViewModel => this.Outlets["right"].viewModel;

    public ViewModelToView VmToViewConverter { get; init; }
}
