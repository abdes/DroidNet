// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger;

public class ViewTypeException(Type viewType, string because)
    : Exception($"The view which type is '{viewType}' is not suitable: {because}")
{
    public Type ViewType => viewType;
}
