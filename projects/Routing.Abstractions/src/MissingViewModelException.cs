// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// An exception thrown when an attempt is made to resolve a ViewModel with a <see langword="null" /> type, or with a type that
/// has not been registered with the Dependency Injector.
/// </summary>
/// <param name="type">The ViewModel type that was being resolved.</param>
public class MissingViewModelException(Type? type)
    : ApplicationException(
        type is null
            ? "attempt to resolve a view model with a null type"
            : $"no view model is registered with the type '{type}'")
{
    /// <summary>
    /// Gets the ViewModel type, which resolution attempt resulted in this exception being thrown.
    /// </summary>
    public Type? ViewModelType => type;
}
