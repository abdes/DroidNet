// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Detail;

using System.Linq;
using DroidNet.Routing.Utils;

/// <summary>
/// Represents the runtime instance of a navigation route, which interface is
/// specified by <see cref="IActiveRoute" />. Such route is part of the router
/// state, built from the router configuration and the navigation url. When
/// "activated", the associated ViewModel is loaded into the corresponding
/// outlet to update the visual state of the application.
/// </summary>
internal sealed partial class ActiveRoute : TreeNode, IActiveRoute
{
    public required IRoute RouteConfig { get; init; }

    public required OutletName Outlet { get; init; }

    public object? ViewModel { get; internal set; }

    public required IReadOnlyList<IUrlSegment> UrlSegments { get; init; }

    public required IParameters Params { get; internal set; }

    public required IParameters QueryParams { get; init; }

    public new IActiveRoute Root => (IActiveRoute)base.Root;

    public new IActiveRoute? Parent => (IActiveRoute?)base.Parent;

    public new IReadOnlyCollection<IActiveRoute> Children
        => base.Children.Cast<ActiveRoute>().ToList().AsReadOnly();

    public new IReadOnlyCollection<IActiveRoute> Siblings
        => base.Siblings.Cast<ActiveRoute>().ToList().AsReadOnly();

    public void AddChild(IActiveRoute route) => CheckNodeAndCallBase(route, this.AddChild);

    public bool RemoveChild(IActiveRoute route) => CheckNodeAndCallBase(route, this.RemoveChild);

    public void AddSibling(IActiveRoute route) => CheckNodeAndCallBase(route, this.AddSibling);

    public bool RemoveSibling(IActiveRoute route) => CheckNodeAndCallBase(route, this.RemoveSibling);

    public void MoveTo(IActiveRoute parent) => CheckNodeAndCallBase(parent, this.MoveTo);
}

/// <summary>
/// Extended implementation details for the <see cref="ActiveRoute" />
/// class.
/// </summary>
internal partial class ActiveRoute
{
    /// <summary>
    /// Gets the <see cref="UrlSegmentGroup" /> from which this
    /// <see cref="ActiveRoute" /> was created.
    /// </summary>
    /// <value>
    /// The <see cref="UrlSegmentGroup" /> from which this <see cref="ActiveRoute" />
    /// was created.
    /// </value>
    internal required IUrlSegmentGroup UrlSegmentGroup { get; init; }

    internal bool IsActivated { get; set; }

    public override string ToString()
        => $"{this.ViewModel?.GetType().Name} (o={this.Outlet} p={this.RouteConfig.Path})";

    /// <summary>
    /// Helper function to call base implementation after checking that the
    /// <paramref name="route" /> is a <see cref="TreeNode" />.
    /// </summary>
    /// <param name="route">
    /// The route object on which the base implementation is to be invoked.
    /// </param>
    /// <param name="baseImpl">
    /// The base implementation method to invoke.
    /// </param>
    /// <exception cref="ArgumentException">
    /// If the <paramref name="route" /> is not a <see cref="TreeNode" />.
    /// </exception>
    private static void CheckNodeAndCallBase(IActiveRoute route, Action<TreeNode> baseImpl)
    {
        if (route is not TreeNode node)
        {
            throw new ArgumentException("only objects implementing ITreeNode are accepted", nameof(route));
        }

        baseImpl(node);
    }

    /// <summary>
    /// Helper function to call base implementation after checking that the
    /// <paramref name="route" /> is a <see cref="TreeNode" />.
    /// </summary>
    /// <typeparam name="T">Return type of the base implementation.</typeparam>
    /// <param name="route">
    /// The route object on which the base implementation is to be invoked.
    /// </param>
    /// <param name="baseImpl">
    /// The base implementation method to invoke.
    /// </param>
    /// <returns>The value returned by the base implementation.</returns>
    /// <exception cref="ArgumentException">
    /// If the <paramref name="route" /> is not a <see cref="TreeNode" />.
    /// </exception>
    private static T CheckNodeAndCallBase<T>(IActiveRoute route, Func<TreeNode, T> baseImpl)
    {
        if (route is not TreeNode node)
        {
            throw new ArgumentException("only objects implementing ITreeNode are accepted", nameof(route));
        }

        return baseImpl(node);
    }
}
