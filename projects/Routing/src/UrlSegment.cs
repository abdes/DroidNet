// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>Represents a path segment in a router URL.</summary>
/// <remarks>
/// In a router URL, the path consists of a sequence of path segments separated
/// by a slash ("/") character. The main part of a segment is its
/// <see cref="Path" />
/// component, but it can also have optional parameters. The semicolon (";")
/// and equals ("=") reserved characters are used to delimit parameters and
/// parameter values applicable to the segment.
/// <para>
/// When serialized, a segment path, as well as any matrix parameter key or
/// value, are percent-encoded in accordance with
/// <see href="https://datatracker.ietf.org/doc/html/rfc3986">RFC 3986</see>.
/// </para>
/// </remarks>
/// <seealso cref="UrlSegmentGroup" />
public class UrlSegment : IUrlSegment
{
    private readonly Dictionary<string, string?> parameters = [];

    /// <summary>
    /// Initializes a new instance of the <see cref="UrlSegment" /> class.
    /// </summary>
    /// <param name="path">The segment's path string.</param>
    /// <param name="parameters">
    /// Optional dictionary of matrix parameters for this segment.
    /// </param>
    /// <exception cref="ArgumentException">
    /// When <paramref name="path" /> is empty or any of the
    /// <paramref name="parameters" /> has an empty name.
    /// </exception>
    public UrlSegment(string path, IDictionary<string, string?>? parameters = null)
    {
        this.Path = path;

        if (parameters == null)
        {
            return;
        }

        foreach (var pair in parameters)
        {
            this.AddParameter(pair.Key, pair.Value);
        }
    }

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
    public string Path { get; }

    /// <summary>
    /// Gets the matrix parameters associated with a URL segment.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Segment matrix parameters can only have a single value and such value
    /// is completely opaque to the <see cref="Router" />. The comma (",")
    /// reserved character can be used as a way to enable specifying multiple
    /// values for a parameter (e.g. <c>key=value1,value2,value3</c>).
    /// </para>
    /// <para>
    /// Segment specific matrix parameters are not in conflict with url query
    /// parameters. They help in keeping segment routing self-contained.
    /// </para>
    /// </remarks>
    /// <value>The matrix parameters associated with a URL segment.</value>
    public IReadOnlyDictionary<string, string?> Parameters => this.parameters.AsReadOnly();

    /// <summary>
    /// Converts the provided <paramref name="path" /> string into an escaped
    /// representation.
    /// </summary>
    /// <remarks>
    /// This method assumes that <paramref name="path" /> string has no escape
    /// sequences in it. It converts all characters except for
    /// <see href="https://datatracker.ietf.org/doc/html/rfc3986">RFC 3986</see>
    /// unreserved characters to their hexadecimal representation.
    /// </remarks>
    /// <param name="path">The path string to serialize.</param>
    /// <returns>The serialized string representation of the path.</returns>
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
    /// it. It converts all characters except for
    /// <see href="https://datatracker.ietf.org/doc/html/rfc3986">RFC 3986</see>
    /// unreserved characters to their hexadecimal representation.
    /// </para>
    /// </remarks>
    /// <param name="parameters">The matrix parameters to serialize.</param>
    /// <returns>
    /// The serialized string representation of the parameters.
    /// </returns>
    public static string SerializeMatrixParams(IDictionary<string, string?> parameters)
        => parameters.Count == 0
            ? string.Empty
            : ';' + string.Join(
                ';',
                parameters.Select(
                    pair => Uri.EscapeDataString(pair.Key) +
                            (pair.Value is not null ? '=' + Uri.EscapeDataString(pair.Value) : string.Empty)));

    /// <summary>Add a parameter to the segment.</summary>
    /// <param name="key">The parameter key. Cannot be empty (zero length).</param>
    /// <param name="value">The parameter value.</param>
    /// <exception cref="ArgumentException">When <paramref name="key" /> is empty.</exception>
    public void AddParameter(string key, string? value)
    {
        if (key.Length == 0)
        {
            throw new ArgumentException("Segment parameters cannot have an empty name.", nameof(key));
        }

        this.parameters.Add(key, value);
    }

    /// <summary>
    /// Serializes the <see cref="UrlSegment" /> into an escaped string
    /// representation.
    /// </summary>
    /// <remarks>
    /// This method assumes that the segment's <see cref="Path" /> and
    /// <see cref="Parameters" /> have no escape sequences in them. It converts
    /// all characters except for
    /// <see href="https://datatracker.ietf.org/doc/html/rfc3986">RFC 3986</see>
    /// unreserved characters to their hexadecimal representation.
    /// </remarks>
    /// <returns>
    /// The serialized string representation of the <see cref="UrlSegment" />.
    /// </returns>
    public override string ToString()
        => $"{SerializePath(this.Path)}{SerializeMatrixParams(this.parameters)}";
}
