// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

public interface IUrlSegment
{
    /// <summary>
    /// Gets the matrix parameters associated with a URL segment.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Segment matrix parameters can only have a single value and such value
    /// is completely opaque to the <see cref="IRouter" />. The comma (",")
    /// reserved character can be used as a way to enable specifying multiple
    /// values for a parameter (e.g. <c>key=value1,value2,value3</c>).
    /// </para>
    /// <para>
    /// Segment specific matrix parameters are not in conflict with url query
    /// parameters. They help in keeping segment routing self-contained.
    /// </para>
    /// </remarks>
    /// <value>The matrix parameters associated with a URL segment.</value>
    IParameters Parameters { get; }

    /// <summary>Gets the path part of a URL segment.</summary>
    /// <remarks>
    /// The <see cref="Path" /> is always defined for a segment and cannot be
    /// empty (zero length).
    /// <para>
    /// When the <see cref="Path" /> of a segment is (".") or (".."), also
    /// known as dot-segments, the segment is used for relative reference
    /// within the routing hierarchy. Such segments are intended for use at the
    /// beginning of a relative-path reference to indicate relative position
    /// within the hierarchical tree of names. They are only interpreted within
    /// the URL path hierarchy and are removed as part of the resolution
    /// process.
    /// </para>
    /// </remarks>
    /// <value>The path part of a URL segment.</value>
    string Path { get; }
}
