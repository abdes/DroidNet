// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger;

public class MissingViewException(Type? viewModelType)
    : Exception(
        viewModelType is null
            ? "attempt to do view resolution for a null view model"
            : $"No view is registered for the view model with type '{viewModelType}'. Missing a DI registration for 'IViewFor<{viewModelType.Name}>'?")
{
    public Type? ViewModelType => viewModelType;
}
