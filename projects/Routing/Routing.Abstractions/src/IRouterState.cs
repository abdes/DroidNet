// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents the current state of the router, encompassing the URL, its parsed tree structure,
/// and the hierarchy of active routes.
/// </summary>
/// <remarks>
/// <para>
/// A router state captures a complete snapshot of the application's navigation structure at a given
/// moment. It maintains the bidirectional relationship between the navigation URL and the tree of
/// active routes, allowing the application to serialize its current state to a URL and reconstruct
/// it later.
/// </para>
/// <para>
/// Each state consists of three key elements: the original URL that created the state, its parsed
/// representation as a URL tree, and a hierarchy of active routes. This three-way relationship ensures
/// consistency between the URL visible to users and the actual view models displayed in the application.
/// Navigation contexts use router states to track which routes are active in their respective targets.
/// </para>
/// </remarks>
public interface IRouterState
{
    /// <summary>
    /// Gets the URL from which this router state was created.
    /// </summary>
    /// <remarks>
    /// This URL serves as the canonical representation of the application state. It can be used
    /// to reconstruct the same view model hierarchy in a new navigation context or to restore
    /// the application state during history navigation.
    /// </remarks>
    string Url { get; }

    /// <summary>
    /// Gets the root of the active route tree.
    /// </summary>
    /// <remarks>
    /// The root node serves as the entry point to the hierarchy of activated routes, each
    /// corresponding to a view model in the application. This tree structure mirrors the
    /// nesting of views in the user interface.
    /// </remarks>
    IActiveRoute RootNode { get; }

    /// <summary>
    /// Gets the parsed representation of the navigation URL.
    /// </summary>
    /// <remarks>
    /// The URL tree provides a structured view of the navigation URL, breaking it down into
    /// segments and parameters that can be matched against route configurations. It serves as
    /// the bridge between the string URL and the route hierarchy.
    /// </remarks>
    IUrlTree UrlTree { get; }
}
