// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

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
    /// <see langword="true" /> if this route is the root route (i.e. its parent is <see langword="null" />);
    /// otherwise, <see langword="false" />.
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
    /// <see langword="true" /> if <paramref name="route" /> was successfully removed;
    /// otherwise, <see langword="false" />. This method also returns false if item was not
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
    /// <see langword="true" /> if <paramref name="route" /> was successfully removed;
    /// otherwise, <see langword="false" />. This method also returns false if the route
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
