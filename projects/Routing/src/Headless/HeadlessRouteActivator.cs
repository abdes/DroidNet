// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Headless;

using DroidNet.Routing.Contracts;

public class HeadlessRouteActivator(IServiceProvider serviceProvider) : AbstractRouteActivator(serviceProvider)
{
    /// <inheritdoc />
    protected override object DoActivateRoute(IActiveRoute route, RouterContext context)
        => throw new NotImplementedException();
}
