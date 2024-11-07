// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using Destructurama.Attributed;

/// <summary>Represents a path segment in a router URL.</summary>
/// <remarks>
/// A router URL is the serialized form of a certain router state, which
/// represents the hierarchy of activated routes at that state. Each route is
/// identified by a path, consisting of a sequence of path segments separated by
/// a slash ("/") character.
/// <para>
/// The main part of a segment is its <see cref="Path" /> component, which is
/// always defined, though <b>it may be empty</b> (zero length). Aside from
/// that, a segment may have parameters, indicated by the presence of a
/// semicolon (';') after its path component. The first parameter is specified
/// after the first semicolon, and subsequent parameters can be specified, each
/// one after a new semicolon character.
/// </para>
/// <para>
/// A segment matrix parameter must have a key and may have an optional value.
/// The equals ('=') reserved character is used to separate the key from the
/// value (when present). The key and the optional value of a parameter are
/// completely opaque to the URL parser, although they add valuable information
/// at the application level and can be interpreted in multiple ways. For
/// example the presence of a key without a value may be interpreted as a
/// boolean flag that is set, eliminating the need for something like
/// <c>flag=true</c>. Another example uses the comma (',') which is not a
/// reserved character in a segment, to indicate multiple values for a
/// parameter.
/// </para>
/// <para>
/// The path segments "." and "..", also known as dot-segments, are defined for
/// relative reference within the hierarchy of routes. When present, these
/// segments are only used to indicate relative position within the hierarchy of
/// segments in a URL path, and are removed as part of the URL resolution
/// process. They <b>cannot have parameters</b>.
/// </para>
/// <para>
/// When serialized, a segment path, as well as any matrix parameter key or
/// value, are percent-encoded in accordance with RFC 3986.
/// </para>
/// </remarks>
/// <seealso cref="UrlSegmentGroup" />
/// <seealso href="https://datatracker.ietf.org/doc/html/rfc3986" />
[LogAsScalar]
public class UrlSegment : IUrlSegment
{
    private readonly Parameters parameters = [];

    /// <summary>
    /// Initializes a new instance of the <see cref="UrlSegment" /> class.
    /// </summary>
    /// <param name="path">The segment's path string.</param>
    /// <param name="parameters">Optional dictionary of matrix parameters for
    /// this segment.</param>
    /// <exception cref="ArgumentException">When <paramref name="path" /> is
    /// empty or any of the <paramref name="parameters" /> has an empty
    /// name.</exception>
    public UrlSegment(string path, IParameters? parameters = null)
    {
        this.Path = path;

        if (parameters == null)
        {
            return;
        }

        foreach (var pair in parameters)
        {
            _ = this.parameters.TryAdd(pair.Name, pair.Value);
        }
    }

    /// <summary>Gets the path component of a URL segment.</summary>
    /// <remarks>
    /// The <see cref="Path" /> is always defined for a segment, though it may
    /// be empty (zero length).
    /// <para>
    /// When the path of a segment is (".") or (".."), also known as
    /// dot-segments, the segment is used for relative reference within the
    /// routing hierarchy. Such segments are intended for use at the beginning
    /// of a relative-path reference to indicate relative position within the
    /// hierarchical tree of names. They are only interpreted within the URL
    /// path hierarchy and are removed as part of the resolution process.
    /// </para>
    /// </remarks>
    public string Path { get; }

    /// <summary>
    /// Gets the matrix parameters associated with a URL segment.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Segment matrix parameters can only have a single value and such value is
    /// completely opaque to the <see cref="Router" />. The comma (",") reserved
    /// character can be used as a way to enable specifying multiple values for
    /// a parameter (e.g. <c>key=value1,value2,value3</c>).
    /// </para>
    /// <para>
    /// Segment specific matrix parameters are not in conflict with url query
    /// parameters. They help in keeping segment routing self-contained.
    /// </para>
    /// </remarks>
    public IParameters Parameters => this.parameters.AsReadOnly();

    /// <summary>
    /// Converts the provided <paramref name="path" /> string into an escaped
    /// representation.
    /// </summary>
    /// <remarks>
    /// This method assumes that <paramref name="path" /> string has no escape
    /// sequences in it. It converts all characters except for RFC 3986
    /// unreserved characters to their hexadecimal representation.
    /// </remarks>
    /// <param name="path">The path string to serialize.</param>
    /// <returns>The serialized string representation of the path.</returns>
    /// <seealso href="https://datatracker.ietf.org/doc/html/rfc3986" />
    public static string SerializePath(string path) => Uri.EscapeDataString(path);

    /// <summary>
    /// Converts the provided <paramref name="parameters" /> into an escaped
    /// string representation.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Each (key, value) pair in the dictionary of parameters corresponds to a
    /// parameter and will be converted to the form <c>;key=value</c> after
    /// escaping the <c>key</c> and the <c>value</c>.
    /// </para>
    /// <para>
    /// This method assumes that <c>key</c> and <c>value</c> strings in any of
    /// the <paramref name="parameters" /> entries have no escape sequences in
    /// it. It converts all characters except for RFC 3986 unreserved characters
    /// to their hexadecimal representation.
    /// </para>
    /// </remarks>
    /// <param name="parameters">The matrix parameters to serialize.</param>
    /// <returns>
    /// The serialized string representation of the parameters.
    /// </returns>
    /// <seealso href="https://datatracker.ietf.org/doc/html/rfc3986" />
    public static string SerializeMatrixParams(IParameters parameters)
        => parameters.Count == 0
            ? string.Empty
            : ';' + string.Join(
                ';',
                parameters.Select(parameter => parameter.ToString()));

    /// <summary>
    /// Serializes the <see cref="UrlSegment" /> into an escaped string
    /// representation.
    /// </summary>
    /// <remarks>
    /// This method assumes that the segment's path and parameters have no
    /// escape sequences in them. It converts all characters except for RFC 3986
    /// unreserved characters to their hexadecimal representation.
    /// </remarks>
    /// <returns>
    /// The serialized string representation of the <see cref="UrlSegment" />.
    /// </returns>
    public override string ToString()
        => SerializePath(this.Path) + SerializeMatrixParams(this.parameters);

    /// <summary>Replace the segment parameters with the given ones.</summary>
    /// <param name="newParameters">The new parameters.</param>
    internal void UpdateParameters(IParameters newParameters)
    {
        this.parameters.Clear();
        foreach (var parameter in newParameters)
        {
            this.parameters.AddOrUpdate(parameter.Name, parameter.Value);
        }
    }
}
