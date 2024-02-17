// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Interface representing an activated route associated with a `ViewModel`
/// loaded in an outlet at a particular moment in time. Also serves as an entry
/// point to traverse the router state tree.
/// </summary>
/// <seealso cref="IActiveRouteTreeNode" />
public interface IActiveRoute : IActiveRouteTreeNode
{
    /// <summary>
    /// Gets the list of URL segments matched by this route.
    /// </summary>
    /// <value>
    /// A list of URL segments, where each element corresponds to a segment
    /// matched by this route. For example if the route is `/users/:id`, then
    /// the list will contain two segments `users` and `:id`.
    /// </value>
    IReadOnlyList<IUrlSegment> UrlSegments { get; }

    /// <summary>Gets the parameters for this route.</summary>
    /// <remarks>
    /// Parameters can be passed either as <see cref="IUrlSegment">url segments</see>,
    /// in such case they are usually referred to as positional parameters, or as
    /// segment matrix parameters are passed as part of each segment in the route
    /// <see cref="UrlSegments" />.
    /// <para>
    /// Any time a route is matched and an active route is created for that
    /// match, the router derives a new set of parameters : the router takes
    /// the positional parameters (e.g., ‘:id’) of the matched URL segments and
    /// the matrix parameters of the last matched URL segment and combines
    /// those.
    /// </para>
    /// <para>
    /// You can access parameters outside the segments forming this route by
    /// traversing the router state tree.
    /// </para>
    /// </remarks>
    /// <value>
    /// A read-only dictionary of the combined parameters in this route.
    /// </value>
    IParameters Params { get; }

    /// <summary>
    /// Gets the query parameters. Query parameters are shared by all routes in
    /// the router state tree and are passed after the '?' character in the
    /// navigation URL.
    /// </summary>
    /// <value>A read-only dictionary of all query parameters.</value>
    IParameters QueryParams { get; }

    /// <summary>
    /// Gets the name of the outlet where the `ViewModel for this
    /// <see cref="IActiveRoute" /> is loaded.
    /// </summary>
    /// <value>
    /// The outlet name where the `ViewModel` for this route is loaded. This
    /// usually corresponds to one of the content outlets specified in the
    /// `View` of this route's <see cref="IActiveRouteTreeNode.Parent" />.
    /// </value>
    OutletName Outlet { get; }

    /// <summary>Gets The ViewModel of the route.</summary>
    /// <value>
    /// The ViewModel of the route; <c>null</c> until the route has been
    /// activated.
    /// </value>
    object? ViewModel { get; }

    /// <summary>
    /// Gets the <see cref="IRoute">route</see> configuration used to match this
    /// route.
    /// </summary>
    /// <value>The route configuration used to match this route.</value>
    /// <remarks>
    /// Provided mostly for debugging and troubleshooting.
    /// </remarks>
    IRoute RouteConfig { get; }
}

/// <summary>
/// The interface for traversing and manipulating the <see cref="IActiveRoute" />
/// tree, specified separately for separation of concerns.
/// </summary>
/// <seealso cref="IActiveRoute" />
public interface IActiveRouteTreeNode
{
    /// <summary>Gets the root of the router state.</summary>
    /// <value>
    /// The root <see cref="IActiveRoute" /> of the router state tree.
    /// </value>
    IActiveRoute Root { get; }

    /// <summary>
    /// Gets the parent of this route in the router state tree.
    /// </summary>
    /// <remarks>
    /// Every route has a parent, except for the root route.
    /// </remarks>
    /// <value>
    /// The parent <see cref="IActiveRoute" /> if any. Null otherwise.
    /// </value>
    IActiveRoute? Parent { get; }

    /// <summary>
    /// Gets the children of this route in the router state tree.
    /// </summary>
    /// <value>
    /// A read-only collection of <see cref="IActiveRoute" />s which are
    /// children of this route.
    /// </value>
    IReadOnlyCollection<IActiveRoute> Children { get; }

    /// <summary>
    /// Gets the siblings of this route in the router state tree.
    /// </summary>
    /// <value>
    /// A read-only collection of <see cref="IActiveRoute" />s which are
    /// siblings of this route.
    /// </value>
    IReadOnlyCollection<IActiveRoute> Siblings { get; }

    /// <summary>
    /// Gets a value indicating whether this route is the root route.
    /// </summary>
    /// <value>
    /// <c>true</c> if this route is the root route (i.e. its parent is <c>null</c>);
    /// otherwise, <c>false</c>.
    /// </value>
    bool IsRoot => this.Parent is null;

    /// <summary>
    /// Add the given <paramref name="route" /> as a child of this route.
    /// </summary>
    /// <remarks>
    /// There should be no assumption regarding the order of the children.
    /// </remarks>
    /// <param name="route">The route to add as a child.</param>
    void AddChild(IActiveRoute route);

    /// <summary>
    /// Remove the first occurence of the given <paramref name="route" /> from
    /// the collection of children of this route.
    /// </summary>
    /// <param name="route">The child route to be removed.</param>
    /// <returns>
    /// <c>true</c> if <paramref name="route" /> was successfully removed;
    /// otherwise, <c>false</c>. This method also returns false if item was not
    /// found in the collection of children.
    /// </returns>
    bool RemoveChild(IActiveRoute route);

    /// <summary>
    /// Add the given <paramref name="route" /> as a sibling of this route.
    /// </summary>
    /// <remarks>
    /// There should be no assumption regarding the order of the siblings.
    /// </remarks>
    /// <param name="route">The route to add as a sibling.</param>
    void AddSibling(IActiveRoute route);

    /// <summary>
    /// Remove the first occurence of the given <paramref name="route" /> from
    /// the collection of children of this route's parent.
    /// </summary>
    /// <param name="route">The sibling route to be removed.</param>
    /// <returns>
    /// <c>true</c> if <paramref name="route" /> was successfully removed;
    /// otherwise, <c>false</c>. This method also returns false if the route
    /// has no parent or if the <paramref name="route" /> was not found in the
    /// collection of siblings.
    /// </returns>
    bool RemoveSibling(IActiveRoute route);

    /// <summary>
    /// Moves this route from its current parent to the new <paramref name="parent" />.
    /// </summary>
    /// <param name="parent">
    /// The new parent under which this route should be located.
    /// </param>
    /// <exception cref="InvalidOperationException">
    /// If the route has no parent (cannot move the root node).
    /// </exception>
    /// <remarks>
    /// This method removes the route from its current parent, then adds it as
    /// a child of the new <paramref name="parent" />.
    /// </remarks>
    void MoveTo(IActiveRoute parent);

    /// <summary>Removes all children of this route.</summary>
    void ClearChildren();
}
