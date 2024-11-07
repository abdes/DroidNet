// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// A configuration object that defines a single route. A set of routes are collected in a <see cref="IRoutes">list of
/// routes</see> to define a <see cref="IRouter">router</see> configuration. The router attempts to match segments of a
/// given URL against each route, using the configuration options defined in this object.
/// <remarks>
/// Supports static and parameterized routes, as well as custom route matching.
/// </remarks>
/// </summary>
public interface IRoute
{
    /// <summary>
    /// Represents a function for matching a route against <see cref="IUrlSegment">segments</see> in a <see cref="IUrlSegmentGroup" />.
    /// </summary>
    /// <param name="segments">
    /// The list of segments, against which the route is to be matched. This could be a subset of the segments in the
    /// <paramref name="group" /> (typically, the remaining unmatched segments so far).
    /// </param>
    /// <param name="group">
    /// The <see cref="IUrlSegmentGroup" /> to which the <paramref name="segments" /> belong.
    /// </param>
    /// <param name="route">
    /// The route being matched.
    /// </param>
    /// <returns>
    /// An instance of <see cref="IMatchResult" /> containing the consumed segments, and any positional parameters found
    /// during the match. Use its <see cref="IMatchResult.IsMatch">IsMatch</see> property to check if it was successful
    /// or not.
    /// </returns>
    public delegate IMatchResult PathMatcher(
        IReadOnlyList<IUrlSegment> segments,
        IUrlSegmentGroup group,
        IRoute route);

    /// <summary>
    /// Gets the path matching method for this route, one of <see cref="PathMatch.Prefix" /> or <see cref="PathMatch.Full" />.
    /// Default is <see cref="PathMatch.Prefix" />.
    /// </summary>
    /// <remarks>
    /// The router checks URL segments from the left to see if the URL matches a given path and stops when
    /// there is a routes config match. Importantly there must still be a config match for each segment of the URL. For
    /// example, '/project/assets/grid' matches the prefix 'project/assets' if one of the route's children matches the
    /// segment 'grid'. That is, the URL `/project/assets/grid` matches the config:
    /// <code>
    /// [{
    ///   Path: 'project/assets',
    ///   Children:
    ///   [
    ///     {
    ///       Path: 'grid',
    ///       ViewModel: AssetGridView
    ///     }
    ///   ]
    /// }]
    /// </code>
    /// but does not match when there are no children as in `[{Path: 'project/assets', ViewModelType: AssetsView}]`.
    /// <para>
    /// The path-match strategy <see cref="PathMatch.Full" /> matches against the entire URL. It is particularly important
    /// to do this when matching empty-path routes, because an empty path is a prefix of any URL.
    /// </para>
    /// </remarks>
    PathMatch MatchMethod { get; }

    /// <summary>Gets the path of the current route.</summary>
    /// <remarks>
    /// The path of a route is a URL path string, but it cannot start with '/' and its value should be unique across all
    /// sibling routes.
    /// </remarks>
    string? Path { get; }

    /// <summary>
    /// Gets the <see cref="PathMatcher" /> to be used to match this route's <see cref="Path" /> to segments in the url.
    /// </summary>
    /// <remarks>
    /// The router implementation has a default route matcher that uses the route's <see cref="Path" /> and <see cref="IRoute.MatchMethod" />. When a custom <see cref="PathMatcher" /> is set, the default matching rules are
    /// completely bypassed, the <see cref="Path" /> and <see cref="MatchMethod" /> no longer have a meaning for the route
    /// matching unless the custom matcher uses them.
    /// </remarks>
    PathMatcher Matcher { get; }

    /// <summary>
    /// Gets the type of the view model for this route.
    /// <remarks>
    /// Can be null if child routes specify a <see cref="ViewModelType" />.
    /// </remarks>
    /// </summary>
    Type? ViewModelType { get; }

    /// <summary>Gets the outlet for which this route is specified.</summary>
    OutletName Outlet { get; }

    /// <summary>Gets the collection of child routes, if any.</summary>
    IRoutes Children { get; }
}

/*
 * USAGE NOTES
 * -----------
 *
 * <remarks>
 * <para>### Simple Configuration:</para>
 * <para>
 * The following route specifies that when navigating to, for example, `/project/assets`, the router loads the
 * 'ProjectViewModel' component with the 'AssetsViewModel' child component in it. Corresponding views will be placed in
 * the primary outlet of their parent ViewModel.
 * </para>
 * <code>
 * [{
 *    Path: "projects",
 *    ViewModelType: typeof(ProjectViewModel),
 *    Children: [{
 *      Path: 'assets',
 *      ViewModelType: typeof(AssetsViewModel)
 *    }]
 * }]
 * </code>
 * <para>### Multiple Outlets:</para>
 * <para>
 * The following route specifies sibling components in multiple outlets for a master-detail page. When navigating to
 * `/project/(left:folders//right:assets)`, the router loads the `ProjectViewModel` and its `ProjectView`, then loads
 * the `ProjectLayoutViewModel` and places its view in the `left` outlet of `ProjectView`, and loads the `AssetsViewModel`
 * and places its view in the `right` outlet of `ProjectView`.
 * </para>
 * <code>
 * [{
 *    Path: "projects",
 *    ViewModelType: typeof(ProjectViewModel),
 *    Children:
 *    [
 *      {
 *        Path: "folders",
 *        ViewModelType: typeof(ProjectLayoutViewModel),
 *        Outlet: "left"
 *      },
 *      {
 *        Path: "assets",
 *        ViewModelType: typeof(AssetsViewModel),
 *        Outlet: "right"
 *      }
 *    ]
 * }]
 * </code>
 * <para>### Empty Path:</para>
 * <para>
 * Empty-path route configurations can be used to instantiate components that do not 'consume' any URL segments. In the
 * following configuration, when navigating to `/project`, the router first loads the `ShellViewModel` and its view,
 * then loads the `ProjectViewModel` and places its view in the `ShellView` primary outlet.
 * </para>
 * <code>
 * [{
 *    Path: "",
 *    ViewModelType: typeof(ShellViewModel),
 *    Children:
 *    [
 *      {
 *        Path: "project",
 *        ViewModelType: typeof(ProjectViewModel),
 *      }
 *    ]
 * }]
 * </code>
 * <para>
 * Empty-path route configurations can also be combined with multiple outlets. The previous example of master-detail
 * view can be configured to always load the `ProjectLayoutViewModel` and its view whenever assets for a specific
 * project folder are requested. For example, when navigating to `/project/assets/scenes`, the router first loads the
 * `ProjectViewModel` and its view, then loads the `ProjectLayoutViewModel` and places its view in the `left` outlet
 * of `ProjectView`, then loads the `AssetsViewModel` and places its view in the `right` outlet. The `AssetsViewModel`
 * will get the `scene` segment as its `:folder` parameter and will show the assets in that folder.
 * </para>
 * <code>
 * [{
 *    Path: "",
 *    ViewModelType: typeof(ProjectViewModel),
 *    Children:
 *    [
 *      {
 *        Path: "",
 *        ViewModelType: typeof(ProjectLayoutViewModel),
 *        Outlet: "left"
 *      },
 *      {
 *        Path: "assets/:folder",
 *        ViewModelType: typeof(AssetsViewModel),
 *        Outlet: "right"
 *      }
 *    ]
 * }]
 * </code>
 * <para>### Route with no ViewModel:</para>
 * <para>
 * A route that does not specify a `ViewModelType` will not result in a `ViewModel` and its `View` being created. They
 * can be used however to share parameters between sibling components. For example, to display the project info and its
 * assets next to each other, we could use the following configuration. When navigating to `/project/10/(assets//side:info)`,
 * the router loads the `ProjectLayoutViewModel`, then the `AssetsViewModel` in the primary outlet and the `InfoViewModel`
 * in the `side` outlet. The router merges the parameters of the parent without ViewModel into the parameters of the
 * children.
 * </para>
 * <code>
 * [{
 *    Path: "/project/:id",
 *    Children:
 *    [
 *      {
 *        Path: "assets",
 *        ViewModelType: typeof(ProjectLayoutViewModel),
 *      },
 *      {
 *        Path: "info",
 *        ViewModelType: typeof(InfoViewModel),
 *        Outlet: "side"
 *      }
 *    ]
 * }]
 * </code>
 * <para>
 * We can push this even further by defining the child routes with an empty `Path` string, as in the following example.
 * With this configuration, navigating to '/project/10' creates the assets child and the info child.
 * </para>
 * <code>
 * [{
 *    Path: "/project/:id",
 *    Children:
 *    [
 *      {
 *        Path: "",
 *        ViewModelType: typeof(ProjectLayoutViewModel),
 *      },
 *      {
 *        Path: "",
 *        ViewModelType: typeof(InfoViewModel),
 *        Outlet: "side"
 *      }
 *    ]
 * }]
 * </code>
 * </remarks>
 */
