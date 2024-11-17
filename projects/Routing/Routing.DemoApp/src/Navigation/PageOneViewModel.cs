// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;

namespace DroidNet.Routing.Demo.Navigation;

/// <summary>
/// A ViewModel for a simple page which has the ability to navigate to other pages with the same navigation view.
/// </summary>
/// <param name="router">The router used for navigation.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage(
    "Maintainability",
    "CA1515:Consider making public types internal",
    Justification = "ViewModel classes must be public because the ViewModel property in the generated code for the view is public")]
public partial class PageOneViewModel(IRouter router) : IRoutingAware
{
    /// <inheritdoc/>
    public IActiveRoute? ActiveRoute { get; set; }

    /// <summary>
    /// Navigates to the next page relative to the current active route.
    /// </summary>
    [RelayCommand]
    public void NextPage()
    {
        if (this.ActiveRoute is not null)
        {
            router.Navigate("2", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }
}
