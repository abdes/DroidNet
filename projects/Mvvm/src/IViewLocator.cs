// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm;

/// <summary>
/// Can be used to resolve a view model type <c>T</c> into the corresponding
/// type which implements <see cref="IViewFor{T}" />.
/// </summary>
public interface IViewLocator
{
    /// <summary>
    /// Determines the 'view' for a ViewModel instance. Uses the
    /// <paramref name="viewModel" />'s runtime type determine the corresponding view
    /// type to use for getting the view instance from the DI service provider.
    /// </summary>
    /// <param name="viewModel">The view model.</param>
    /// <returns>The 'view' associated with the given view model.</returns>
    object? ResolveView(object viewModel);

    /// <summary>
    /// Determines the 'view' for a ViewModel type <typeparamref name="T" />.
    /// </summary>
    /// <typeparam name="T">The view model type.</typeparam>
    /// <returns>
    /// The 'view' associated with the given view model type.
    /// </returns>
    IViewFor<T>? ResolveView<T>()
        where T : class;
}
