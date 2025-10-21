// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents a single segment in a URL path, consisting of a path component and optional matrix parameters.
/// </summary>
/// <remarks>
/// <para>
/// URL segments form the basic components of routing paths, with each segment potentially carrying
/// both a path value and associated matrix parameters. For example, in the URL
/// "/users;role=admin/123;detail=full", we have two segments: "users" with its role parameter, and
/// "123" with its detail parameter. These segments work together to create meaningful navigation
/// paths while maintaining clear parameter scope.
/// </para>
/// </remarks>
public interface IUrlSegment
{
    /// <summary>
    /// Gets the matrix parameters associated with this URL segment.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Matrix parameters provide segment-specific metadata that remains distinct from URL query
    /// parameters. While query parameters apply globally to a URL, matrix parameters belong
    /// specifically to their containing segment, maintaining clear parameter scope and preventing
    /// naming conflicts. For instance, in "/users;role=admin/123;detail=full", the role parameter
    /// belongs exclusively to the "users" segment, while the detail parameter is scoped to "123".
    /// </para>
    /// <para>
    /// Multiple values for the same parameter can be specified in two ways: using comma-separated
    /// values like "roles=admin,user,guest", or by repeating the parameter multiple times like
    /// "roles=admin;roles=user;roles=guest". Both approaches are valid and preserve the parameter
    /// values exactly as provided. The router treats these values as opaque strings and makes no
    /// assumptions about their format or meaning.
    /// </para>
    /// </remarks>
    public IParameters Parameters { get; }

    /// <summary>
    /// Gets the path component of this URL segment.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The path component serves as the primary identifier for a segment and must always be
    /// present. It appears before any matrix parameters and follows standard URL encoding rules.
    /// Special meaning is given to dot-segments: "." refers to the current hierarchy level, while
    /// ".." refers to the parent level. These dot-segments play a crucial role in relative URL
    /// navigation, allowing for precise traversal of the routing hierarchy. They are processed
    /// during URL resolution and do not persist in the final routing structure.
    /// </para>
    /// </remarks>
    public string Path { get; }
}
