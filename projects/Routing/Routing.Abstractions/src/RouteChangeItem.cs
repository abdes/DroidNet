// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

public class RouteChangeItem
{
    public required OutletName Outlet { get; init; }

    public required RouteChangeAction ChangeAction { get; init; }

    public IParameters? Parameters { get; init; }

    public Type? ViewModelType { get; init; }
}
