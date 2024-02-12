// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

public interface IRouterContext
{
    /// <summary>
    /// Gets the name of the navigation target where the root content should be
    /// loaded.
    /// </summary>
    /// <value>
    /// The name of the navigation target where the root content should be
    /// loaded.
    /// </value>
    Target Target { get; }
}
