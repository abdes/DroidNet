// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple.Navigation;

using DroidNet.Hosting.Generators;
using Microsoft.Extensions.DependencyInjection;

[InjectAs(ServiceLifetime.Singleton)]
public class PageThreeViewModel : IRoutingAware
{
    public IActiveRoute? ActiveRoute { get; set; }
}
