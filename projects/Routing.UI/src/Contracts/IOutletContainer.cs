// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.UI.Contracts;

/// <summary>
/// Represents a view model that has outlets which can be used to load the
/// content for a specific route.
/// </summary>
public interface IOutletContainer
{
    /// <summary>
    /// Loads a view model into the outlet using a view model type.
    /// </summary>
    /// <param name="viewModel">The view model to load.</param>
    /// <param name="outletName">
    /// The name of the outlet in which to load the viewmodel content.
    /// </param>
    /// <remarks>
    /// Throw a <see cref="InvalidOperationException" /> if loading the view
    /// model by its <see cref="Type" /> is not supported.
    /// </remarks>
    void LoadContent(object viewModel, string? outletName = null);
}
