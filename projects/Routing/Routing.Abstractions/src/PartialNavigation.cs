// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <inheritdoc />
/// <remarks>A partial navigation request uses a relative URL or a set of
/// changes to the router state. In both cases, a relative route must be
/// provided. It should be expected that after the navigation completes, the
/// router state is only partially changes. This could help optimize the amount
/// of work needed to update the application.</remarks>
public class PartialNavigation : NavigationOptions
{
    public override required IActiveRoute? RelativeTo { get; init; }
}
