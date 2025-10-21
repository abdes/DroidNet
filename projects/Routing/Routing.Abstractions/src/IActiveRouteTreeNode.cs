// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Defines operations for traversing and manipulating the router state tree structure.
/// </summary>
/// <remarks>
/// Provides access to the hierarchical relationships between routes and operations to modify the tree
/// structure. This interface is separated from <see cref="IActiveRoute"/> for separation of concerns,
/// allowing independent manipulation of the tree structure.
/// </remarks>
/// <seealso cref="IActiveRoute"/>
public interface IActiveRouteTreeNode
{
    /// <summary>
    /// Gets the root node of the router state tree.
    /// </summary>
    /// <value>
    /// The root <see cref="IActiveRoute"/> from which all other routes in the current router state
    /// descend.
    /// </value>
    public IActiveRoute Root { get; }

    /// <summary>
    /// Gets the parent route of this route in the router state tree.
    /// </summary>
    /// <remarks>
    /// Every route has a parent except for the root route, which represents the top of the routing
    /// hierarchy.
    /// </remarks>
    /// <value>
    /// The parent <see cref="IActiveRoute"/> if this route has a parent; otherwise, null for the root
    /// route.
    /// </value>
    public IActiveRoute? Parent { get; }

    /// <summary>
    /// Gets all child routes of this route in the router state tree.
    /// </summary>
    /// <value>
    /// A read-only collection of <see cref="IActiveRoute"/>s that are direct descendants of this
    /// route in the hierarchy.
    /// </value>
    public IReadOnlyCollection<IActiveRoute> Children { get; }

    /// <summary>
    /// Gets all sibling routes of this route in the router state tree.
    /// </summary>
    /// <value>
    /// A read-only collection of <see cref="IActiveRoute"/>s that share the same parent as this
    /// route.
    /// </value>
    public IReadOnlyCollection<IActiveRoute> Siblings { get; }

    /// <summary>
    /// Gets a value indicating whether this route is the root of the router state tree.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if this route has no parent (is the root);
    /// otherwise, <see langword="false"/>.
    /// </value>
    public bool IsRoot => this.Parent is null;

    /// <summary>
    /// Adds a route as a child of this route.
    /// </summary>
    /// <remarks>
    /// The order of children in the collection is not guaranteed and should not be relied upon.
    /// </remarks>
    /// <param name="route">The route to add as a child of this route.</param>
    public void AddChild(IActiveRoute route);

    /// <summary>
    /// Removes a child route from this route's children.
    /// </summary>
    /// <param name="route">The child route to remove.</param>
    /// <returns>
    /// <see langword="true"/> if the route was found and removed from the children collection;
    /// <see langword="false"/> if the route was not found.
    /// </returns>
    public bool RemoveChild(IActiveRoute route);

    /// <summary>
    /// Adds a route as a sibling of this route.
    /// </summary>
    /// <remarks>
    /// The order of siblings in the collection is not guaranteed and should not be relied upon.
    /// </remarks>
    /// <param name="route">The route to add as a sibling.</param>
    /// <exception cref="InvalidOperationException">
    /// Thrown when attempting to add a sibling to a root route that has no parent.
    /// </exception>
    public void AddSibling(IActiveRoute route);

    /// <summary>
    /// Removes a sibling route from this route's parent's children.
    /// </summary>
    /// <param name="route">The sibling route to remove.</param>
    /// <returns>
    /// <see langword="true"/> if the route was found and removed from the parent's children;
    /// <see langword="false"/> if this route has no parent or if the specified route was not found.
    /// </returns>
    public bool RemoveSibling(IActiveRoute route);

    /// <summary>
    /// Moves this route from its current parent to a new parent in the router state tree.
    /// </summary>
    /// <remarks>
    /// Removes this route from its current parent's children and adds it to the new parent's
    /// children collection.
    /// </remarks>
    /// <param name="parent">The new parent route under which this route should be placed.</param>
    /// <exception cref="InvalidOperationException">
    /// Thrown when attempting to move the root route which has no parent.
    /// </exception>
    public void MoveTo(IActiveRoute parent);

    /// <summary>
    /// Removes all child routes from this route.
    /// </summary>
    public void ClearChildren();
}
