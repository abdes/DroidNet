// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Demo.Navigation;

using CommunityToolkit.Mvvm.Input;

/// <summary>A ViewModel for a simple page which has the ability to navigate to other pages with the same navigation view.</summary>
/// <param name="router">The router used for navigation.</param>
public partial class PageTwoViewModel(IRouter router) : IRoutingAware
{
    public IActiveRoute? ActiveRoute { get; set; }

    [RelayCommand]
    public void PreviousPage()
    {
        if (this.ActiveRoute is not null)
        {
            router.Navigate("1", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }
}
