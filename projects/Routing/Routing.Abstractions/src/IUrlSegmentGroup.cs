// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace DroidNet.Routing;

/// <summary>
/// Represents a hierarchical group of URL segments, supporting both primary and auxiliary navigation paths.
/// </summary>
/// <remarks>
/// <para>
/// URL segment groups organize URL segments into hierarchical structures that support complex
/// navigation scenarios. Each group can contain both sequential segments for its primary navigation
/// path and named children targeting different outlets. For example, in a URL like
/// "(main:users/123//side:details)", we have two parallel segment groups: one for the main outlet
/// showing user information, and another for a side panel showing details.
/// </para>
/// <para>
/// The hierarchical nature of segment groups enables sophisticated layouts where different parts of
/// the URL control different regions of the application. Parent-child relationships are maintained
/// automatically as the URL structure is built, ensuring proper context for navigation and route
/// matching.
/// </para>
/// </remarks>
public interface IUrlSegmentGroup
{
    /// <summary>
    /// Gets the segments belonging to this group's primary navigation path.
    /// </summary>
    /// <remarks>
    /// These segments represent the sequential parts of the URL path for this group. For instance,
    /// in a URL path "users/123", a segment group would contain two segments. The segments maintain
    /// their original order, which is crucial for proper route matching and parameter extraction.
    /// </remarks>
    ReadOnlyCollection<IUrlSegment> Segments { get; }

    /// <summary>
    /// Gets the child segment groups, each targeting a specific named outlet.
    /// </summary>
    /// <remarks>
    /// Child groups enable parallel navigation paths within the same URL. Each child is identified
    /// by an outlet name, allowing different parts of the URL to control different regions of the
    /// application interface. The router uses this structure to match routes and activate view
    /// models in their appropriate outlets.
    /// </remarks>
    IReadOnlyDictionary<OutletName, IUrlSegmentGroup> Children { get; }

    /// <summary>
    /// Gets the child segment groups in a specialized order with the primary outlet's group first.
    /// </summary>
    /// <remarks>
    /// This ordered view of children prioritizes the primary navigation path while maintaining access
    /// to auxiliary paths. The ordering is particularly important during route matching, where primary
    /// routes should be considered before auxiliary ones to ensure consistent navigation behavior.
    /// </remarks>
    ReadOnlyCollection<KeyValuePair<OutletName, IUrlSegmentGroup>> SortedChildren { get; }

    /// <summary>
    /// Gets the parent segment group in the URL hierarchy.
    /// </summary>
    /// <remarks>
    /// The parent reference is automatically maintained as segment groups are assembled into a tree
    /// structure. This bidirectional relationship enables both downward and upward traversal of the
    /// URL hierarchy, which is essential for resolving relative paths and maintaining navigation context.
    /// </remarks>
    IUrlSegmentGroup? Parent { get; }
}
