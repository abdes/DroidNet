// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.View;

/// <summary>
/// This base class is only used by the <see cref="DefaultViewLocator" />.
/// Implement <see cref="IViewFor{T}" /> instead.
/// </summary>
public interface IViewFor
{
    /// <summary>
    /// Raised when the ViewModel associated with this View changes.
    /// </summary>
    /// <remarks>
    /// The typical implementation of a View will have a dependency property
    /// for the ViewModel and will wire value changes to that property to
    /// automatically raise the <see cref="ViewModelChanged" /> event.
    /// </remarks>
    event EventHandler? ViewModelChanged;

    /// <summary>Gets or sets the View Model associated with the View.</summary>
    /// <value>The View Model associated with the View.</value>
    object ViewModel { get; set; }
}

/// <summary>
/// Implement this interface in a View to support automatic view location for
/// routing and binding.
/// </summary>
/// <typeparam name="T">
/// The type of ViewModel associated with this View.
/// </typeparam>
public interface IViewFor<T> : IViewFor
    where T : class
{
    /// <summary>
    /// Gets or sets the ViewModel corresponding to this specific View.
    /// </summary>
    /// <value>The ViewModel corresponding to this specific View.</value>
    new T ViewModel { get; set; }
}
