// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Contracts;

public interface IRouterState
{
    /// <summary>
    /// Gets the url from which this router state was created.
    /// </summary>
    /// <value>The url from which this router state was created.</value>
    string Url { get; }

    IActiveRoute Root { get; }

    UrlTree UrlTree { get; }
}
