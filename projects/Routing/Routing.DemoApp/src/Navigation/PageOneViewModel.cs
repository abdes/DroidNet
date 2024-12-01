// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;

namespace DroidNet.Routing.Demo.Navigation;

/// <summary>
/// A ViewModel for a simple page which has the ability to navigate to other pages with the same navigation view.
/// </summary>
/// <param name="router">The router used for navigation.</param>
public partial class PageOneViewModel(IRouter router) : IRoutingAware
{
    private IActiveRoute? activeRoute;

    /// <inheritdoc/>
    public Task OnNavigatedToAsync(IActiveRoute route)
    {
        this.activeRoute = route;
        return Task.CompletedTask;
    }

    /// <summary>
    /// Navigates to the next page relative to the current active route.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [RelayCommand]
    public async Task NextPageAsync()
    {
        if (this.activeRoute is not null)
        {
            await router.NavigateAsync("2", new PartialNavigation() { RelativeTo = this.activeRoute.Parent }).ConfigureAwait(true);
        }
    }
}
