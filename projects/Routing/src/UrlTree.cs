// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics;

/// <summary>Represented a parsed router URL.</summary>
/// <remarks>
/// Since the <see cref="UrlTree" /> is from a certain point of view a
/// representation of the <see cref="Router" /> state, the URL is a serialized
/// representation of that state.
/// </remarks>
public class UrlTree : IUrlTree
{
    /// <summary>
    /// Initializes a new instance of the <see cref="UrlTree" /> class.
    /// </summary>
    /// <param name="root">
    /// Optional root <see cref="UrlSegmentGroup" />. If not specified, the
    /// <see cref="Root" /> property will be initialized with no segments and no
    /// children.
    /// </param>
    /// <param name="queryParams">
    /// Optional dictionary of query params. If not present, the
    /// <see cref="QueryParams" />
    /// property will be initialized with an empty collection.
    /// </param>
    public UrlTree(UrlSegmentGroup? root = null, IParameters? queryParams = null)
    {
        this.Root = root ?? new UrlSegmentGroup([]);
        this.QueryParams = queryParams ?? ReadOnlyParameters.Empty;
        Debug.Assert(
            this.Root.Segments.Count == 0,
            "The root `UrlSegmentGroup` should not contain segments. Instead, the segments should be in the `children` so they can be associated with a named outlet.");
    }

    /// <summary>
    /// Gets the root <see cref="UrlSegmentGroup" /> of the <see cref="UrlTree" />.
    /// </summary>
    /// <value>The root <see cref="UrlSegmentGroup" />.</value>
    public IUrlSegmentGroup Root { get; }

    /// <inheritdoc />
    public IParameters QueryParams { get; }

    /// <summary>Gets a value indicating whether this tree represents a relative URL.</summary>
    public bool IsRelative { get; init; }

    /// <summary>
    /// Serializes the <see cref="UrlTree" /> into a string, using
    /// percent-encoding to escape special characters as specified in
    /// <see href="https://datatracker.ietf.org/doc/html/rfc3986">RFC 3986</see>.
    /// </summary>
    /// <returns>
    /// A string representing the serialized form of the <see cref="UrlTree" />.
    /// </returns>
    /// <seealso cref="DefaultUrlSerializer" />
    public override string ToString() => DefaultUrlSerializer.Instance.Serialize(this);
}
