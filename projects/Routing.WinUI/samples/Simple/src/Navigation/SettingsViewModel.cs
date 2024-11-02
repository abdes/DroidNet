// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple.Navigation;

public class SettingsViewModel : IRoutingAware
{
    public IActiveRoute? ActiveRoute { get; set; }
}
