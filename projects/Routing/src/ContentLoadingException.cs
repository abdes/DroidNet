// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Exception that can be used to report failure to load the content from the
/// ViewModel <paramref name="viewModel" /> into the outlet with the name
/// <paramref name="outletName" />.
/// </summary>
/// <param name="outletName">The name of the outlet in which content was being loaded.</param>
/// <param name="viewModel">The ViewModel of the content.</param>
/// <param name="because">Explanatory message describing the reason for the failure.</param>
/// <param name="innerException">Optional exception at the origin of the failure.</param>
public class ContentLoadingException(
    string outletName,
    object? viewModel,
    string because,
    Exception? innerException = null)
    : Exception(
        $"could not load view model content `{viewModel}` into outlet `{outletName}` because: {because}",
        innerException)
{
    /// <summary>
    /// Gets the name of the outlet in which content was being loaded.
    /// </summary>
    /// <value>
    /// The name of the outlet in which content was being loaded. This is
    /// usually the value used in the <see cref="Route.Outlet" /> property.
    /// </value>
    public string OutletName { get; } = outletName;

    /// <summary>
    /// Gets the ViewModel of the content.
    /// </summary>
    /// <value>
    /// The ViewModel of the content. This is usually an instance of type
    /// specified in the <see cref="Route.ViewModelType" /> property.
    /// </value>
    public object? ViewModel { get; } = viewModel;
}
