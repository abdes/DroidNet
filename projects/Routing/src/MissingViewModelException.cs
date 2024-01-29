// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

public class MissingViewModelException(Type? type)
    : Exception(
        type is null
            ? "attempt to resolve a view model with a null type"
            : $"no view model is registered with the type '{type}'")
{
    public Type? ViewModelType => type;
}
