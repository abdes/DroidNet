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
public partial class PageTwoViewModel(IRouter router) : IRoutingAware
{
    /// <inheritdoc/>
    public IActiveRoute? ActiveRoute { get; set; }

    /// <summary>
    /// Navigates to the previous page in the navigation stack.
    /// </summary>
    /// <remarks>
    /// This method uses the router to navigate to the previous page relative to the current active route's parent.
    /// </remarks>
    [RelayCommand]
    public void PreviousPage()
    {
        if (this.ActiveRoute is not null)
        {
            router.Navigate("1", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }
}
