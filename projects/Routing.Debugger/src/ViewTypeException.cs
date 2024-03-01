// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger;

using DroidNet.Routing.View;

/// <summary>
/// Thrown when a View resolved for a certain ViewModel has a type that does not satisfy the requirements, such as not
/// implementing the <see cref="IViewFor"/> or does not derive from a type that can be set as Content for an outlet.
/// </summary>
/// <param name="viewType">The type of the View.</param>
/// <param name="because">The reason why the View Type is not suitable.</param>
public class ViewTypeException(Type viewType, string because)
    : ApplicationException($"The view which type is '{viewType}' is not suitable: {because}")
{
    public Type ViewType => viewType;
}
