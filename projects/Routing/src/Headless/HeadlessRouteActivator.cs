// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Headless;

public class HeadlessRouteActivator(IServiceProvider serviceProvider) : AbstractRouteActivator(serviceProvider)
{
    /// <inheritdoc />
    protected override void DoActivateRoute(IActiveRoute route, RouterContext context)
        => throw new NotImplementedException();
}
