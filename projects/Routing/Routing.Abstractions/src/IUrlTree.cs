// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents a parsed navigation URL structured as a hierarchical tree.
/// </summary>
/// <remarks>
/// <para>
/// A URL tree provides a structured view of a navigation URL, breaking it down into segments, parameters,
/// and outlet-specific paths. For example, a complex URL like "/users/123;details=full?tab=profile//
/// (side:comments//bottom:related)" becomes a tree structure where the main path, auxiliary outlet paths,
/// matrix parameters, and query parameters are organized for efficient route matching and navigation.
/// </para>
/// <para>
/// The tree structure naturally represents both simple linear paths and complex multi-outlet navigation
/// scenarios. Through its root segment group, it maintains the hierarchical relationships between URL
/// parts while preserving query parameters that apply to the entire URL. The router uses this parsed
/// structure to match routes and determine which view models to activate in which outlets.
/// </para>
/// </remarks>
public interface IUrlTree
{
    /// <summary>
    /// Gets the root segment group of the parsed URL tree.
    /// </summary>
    /// <remarks>
    /// The root group serves as the entry point to the URL's hierarchical structure. It contains
    /// the primary navigation path's segments and any auxiliary paths targeting specific outlets.
    /// Through this root, the entire URL structure can be traversed, enabling sophisticated route
    /// matching and navigation.
    /// </remarks>
    IUrlSegmentGroup Root { get; }

    /// <summary>
    /// Gets the query parameters from the URL.
    /// </summary>
    /// <remarks>
    /// Unlike matrix parameters which belong to specific segments, query parameters apply to the
    /// entire URL. They appear after the '?' character and can carry state that affects the entire
    /// navigation context. The router makes these parameters available to all activated routes,
    /// regardless of their position in the hierarchy.
    /// </remarks>
    IParameters QueryParams { get; }

    /// <summary>
    /// Gets a value indicating whether this tree represents a relative URL.
    /// </summary>
    /// <remarks>
    /// Relative URLs, such as "details" or "../settings", are interpreted within the context of a
    /// base URL. This property helps the router determine whether the URL should be resolved
    /// against the current navigation state before processing. When <see langword="true"/>, the
    /// router combines this tree with the current state to produce an absolute navigation target.
    /// </remarks>
    bool IsRelative { get; }
}
