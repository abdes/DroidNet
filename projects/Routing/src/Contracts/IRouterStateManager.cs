// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Contracts;

public interface IRouterStateManager
{
    /// <summary>
    /// Generates <see cref="IRouterState" /> from a <see cref="UrlTree" /> by
    /// attempting to match each node in the tree to the routes in the router
    /// config.
    /// </summary>
    /// <param name="urlTree">
    /// The <see cref="UrlTree" /> from which to generate the new router state.
    /// </param>
    /// <returns>
    /// A <see cref="IRouterState" />, not yet activated, corresponding to the
    /// given <paramref name="urlTree" />.
    /// </returns>
    /// <remarks>
    /// When trying to match a URL to a route, the router looks at the
    /// unmatched segments of the URL and tries to find a path that will match,
    /// or consume a segment. The router takes a depth-first approach to
    /// matching URL segments with paths, having to backtrack sometimes to
    /// continue trying the next routes if the current one fails to match. This
    /// means that the first path of routes to fully consume a URL wins. You
    /// must take care with how you structure your router configuration, as
    /// there is no notion of specificity or importance amongst routes — the
    /// first match always wins. Order matters. Once all segments of the URL
    /// have been consumed, we say that a match has occurred.
    /// </remarks>
    IRouterState CreateFromUrlTree(UrlTree urlTree);
}
