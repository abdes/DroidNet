// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm;

/// <summary>
/// Represents a non-generic instance of <see cref="IViewFor{T}" />. Prefer to use the generic version whenever possible and limit
/// the use of the non-generic version to when the type of ViewModel is not important.
/// </summary>
public interface IViewFor
{
    /// <summary>Gets or sets the View Model associated with the View.</summary>
    object? ViewModel { get; set; }
}

/// <summary>Implement this interface in a View to support automatic view location for routing and binding.</summary>
/// <typeparam name="T">The type of ViewModel associated with this View.</typeparam>
public interface IViewFor<T> : IViewFor
    where T : class
{
    /// <summary>Raised when the ViewModel associated with this View changes.</summary>
    /// <remarks>
    /// The typical implementation of a View will have a dependency property for the ViewModel, and will wire value changes to
    /// that property to automatically raise the <see cref="ViewModelChanged" /> event.
    /// </remarks>
    event EventHandler<ViewModelChangedEventArgs<T>>? ViewModelChanged;

    /// <inheritdoc cref="IViewFor.ViewModel" />
    new T? ViewModel { get; set; }
}
