// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <inheritdoc />
/// <remarks>A full navigation request uses an absolute URL, and as such, it
/// does not have a <see cref="NavigationOptions.RelativeTo" /> route. It should
/// be expected that after the navigation completes, the router state is
/// completely new.</remarks>
public class FullNavigation : NavigationOptions;
