// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;

namespace DroidNet.Routing.Demo.Navigation;

/// <summary>
/// A ViewModel for a simple page which has the ability to navigate to other pages with the same navigation view.
/// </summary>
/// <param name="router">The router used for navigation.</param>
public partial class PageTwoViewModel(IRouter router) : IRoutingAware
{
    private IActiveRoute? activeRoute;

    /// <inheritdoc/>
    public Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        this.activeRoute = route;
        return Task.CompletedTask;
    }

    /// <summary>
    /// Navigates to the previous page in the navigation stack.
    /// </summary>
    /// <remarks>
    /// This method uses the router to navigate to the previous page relative to the current active route's parent.
    /// </remarks>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [RelayCommand]
    public async Task PreviousPageAsync()
    {
        if (this.activeRoute is not null)
        {
            await router.NavigateAsync("1", new PartialNavigation() { RelativeTo = this.activeRoute.Parent }).ConfigureAwait(true);
        }
    }
}
