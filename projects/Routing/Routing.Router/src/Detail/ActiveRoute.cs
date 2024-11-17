// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Destructurama.Attributed;
using DroidNet.Routing.Utils;

namespace DroidNet.Routing.Detail;

/// <summary>
/// Represents the runtime instance of a navigation route, which interface is specified by
/// <see cref="IActiveRoute" />. Such route is part of the router state, built from the router
/// configuration and the navigation url. When "activated", the associated ViewModel is loaded into
/// the corresponding outlet to update the visual state of the application.
/// </summary>
internal sealed partial class ActiveRoute : TreeNode, IActiveRoute
{
    /// <inheritdoc/>
    public required IRoute Config { get; init; }

    /// <inheritdoc/>
    [LogAsScalar]
    public required OutletName Outlet { get; init; }

    /// <inheritdoc/>
    [LogAsScalar]
    public object? ViewModel { get; internal set; }

    /// <inheritdoc/>
    public required IReadOnlyList<IUrlSegment> Segments { get; init; }

    /// <inheritdoc/>
    public required IParameters Params { get; internal set; }

    /// <inheritdoc/>
    public required IParameters QueryParams { get; init; }

    /// <inheritdoc/>
    [NotLogged]
    public new IActiveRoute Root => (IActiveRoute)base.Root;

    /// <inheritdoc/>
    [NotLogged]
    public new IActiveRoute? Parent => (IActiveRoute?)base.Parent;

    /// <inheritdoc/>
    public new IReadOnlyCollection<IActiveRoute> Children
        => base.Children.Cast<ActiveRoute>().ToList().AsReadOnly();

    /// <inheritdoc/>
    [NotLogged]
    public new IReadOnlyCollection<IActiveRoute> Siblings
        => base.Siblings.Cast<ActiveRoute>().ToList().AsReadOnly();

    /// <inheritdoc/>
    public void AddChild(IActiveRoute route) => CheckNodeAndCallBase(route, this.AddChild);

    /// <summary>
    /// Adds multiple child routes to the current route.
    /// </summary>
    /// <param name="routes">The collection of routes to add as children.</param>
    public void AddChildren(IEnumerable<IActiveRoute> routes)
    {
        foreach (var route in routes)
        {
            CheckNodeAndCallBase(route, this.AddChild);
        }
    }

    /// <inheritdoc/>
    public bool RemoveChild(IActiveRoute route) => CheckNodeAndCallBase(route, this.RemoveChild);

    /// <inheritdoc/>
    public void AddSibling(IActiveRoute route) => CheckNodeAndCallBase(route, this.AddSibling);

    /// <inheritdoc/>
    public bool RemoveSibling(IActiveRoute route) => CheckNodeAndCallBase(route, this.RemoveSibling);

    /// <inheritdoc/>
    public void MoveTo(IActiveRoute parent) => CheckNodeAndCallBase(parent, this.MoveTo);
}

/// <summary>
/// Extended implementation details for the <see cref="ActiveRoute" />
/// class.
/// </summary>
[LogAsScalar]
internal partial class ActiveRoute
{
    /// <summary>
    /// Gets the <see cref="SegmentGroup" /> from which this
    /// <see cref="ActiveRoute" /> was created.
    /// </summary>
    /// <value>
    /// The <see cref="SegmentGroup" /> from which this <see cref="ActiveRoute" />
    /// was created.
    /// </value>
    internal required IUrlSegmentGroup SegmentGroup { get; init; }

    /// <summary>
    /// Gets or sets a value indicating whether this route is activated.
    /// </summary>
    internal bool IsActivated { get; set; }

    /// <inheritdoc/>
    public override string ToString()
        => $"vm={this.ViewModel?.GetType().Name} (o={this.Outlet} p={this.Config.Path})";

    /// <summary>
    /// Helper function to call base implementation after checking that the <paramref name="route" /> is a <see cref="TreeNode" />.
    /// </summary>
    /// <param name="route">The route object on which the base implementation is to be invoked.</param>
    /// <param name="baseImpl">The base implementation method to invoke.</param>
    /// <exception cref="ArgumentException">If the <paramref name="route" /> is not a <see cref="TreeNode" />.</exception>
    private static void CheckNodeAndCallBase(IActiveRoute route, Action<TreeNode> baseImpl)
    {
        if (route is not TreeNode node)
        {
            throw new ArgumentException("only objects implementing ITreeNode are accepted", nameof(route));
        }

        baseImpl(node);
    }

    /// <summary>
    /// Helper function to call base implementation after checking that the <paramref name="route" /> is a <see cref="TreeNode" />.
    /// </summary>
    /// <typeparam name="T">Return type of the base implementation.</typeparam>
    /// <param name="route">The route object on which the base implementation is to be invoked.</param>
    /// <param name="baseImpl">The base implementation method to invoke.</param>
    /// <returns>The value returned by the base implementation.</returns>
    /// <exception cref="ArgumentException">If the <paramref name="route" /> is not a <see cref="TreeNode" />.</exception>
    private static T CheckNodeAndCallBase<T>(IActiveRoute route, Func<TreeNode, T> baseImpl) => route is not TreeNode node
            ? throw new ArgumentException("only objects implementing ITreeNode are accepted", nameof(route))
            : baseImpl(node);
}
