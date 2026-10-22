// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// The rule by which routing lets go of the view models it shows.
/// </summary>
public static class RoutedViewModels
{
    /// <summary>
    /// Gets a value indicating whether a view model is retained by its owner, which disposes it.
    /// </summary>
    /// <param name="viewModel">The view model.</param>
    /// <returns><see langword="true"/> when the view model's type carries <see cref="RetainedViewModelAttribute"/>.</returns>
    public static bool IsRetained(object? viewModel)
        => viewModel?.GetType().IsDefined(typeof(RetainedViewModelAttribute), inherit: true) == true;

    /// <summary>
    /// Lets go of a view model that routing no longer shows: disposes it, unless its owner retains it.
    /// </summary>
    /// <param name="viewModel">The view model, if any.</param>
    public static void Release(object? viewModel)
    {
        if (viewModel is IDisposable disposable && !IsRetained(viewModel))
        {
            disposable.Dispose();
        }
    }
}
