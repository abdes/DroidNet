// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// The default implementation of <see cref="IUrlSerializer" />.
/// </summary>
/// <example>
/// <code>
/// /Home
/// modal:About
/// /Documentation/GettingStarted;toc=true
/// /Documentation(popup:Feedback)
/// /Home(sidebar:Chat)
/// </code>
/// </example>
public class DefaultUrlSerializer(IUrlParser parser) : IUrlSerializer
{
    private static readonly Lazy<DefaultUrlSerializer> Lazy = new(
        () => new DefaultUrlSerializer(new DefaultUrlParser()));

    /// <summary>
    /// Gets the single instance of the <see cref="DefaultUrlSerializer" />
    /// class.
    /// </summary>
    /// <remarks>
    /// The single instance use the <see cref="Lazy{T}" /> pattern, which
    /// guarantees that all its public members are thread safe, and is
    /// therefore suitable for implementing a thread safe singleton.
    /// </remarks>
    /// <value>
    /// The single instance of the <see cref="DefaultUrlSerializer" /> class.
    /// </value>
    public static DefaultUrlSerializer Instance => Lazy.Value;

    /// <inheritdoc />
    public IUrlTree Parse(string url) => parser.Parse(url);

    /// <inheritdoc />
    public string Serialize(IUrlTree tree)
    {
        var path = tree.Root.ToString();
        var queryParams = SerializeQueryParams(tree.QueryParams);
        return string.Concat(tree.IsRelative ? string.Empty : '/', path, queryParams);
    }

    /// <summary>
    /// Converts the provided <paramref name="parameters" /> into an escaped
    /// string representation.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The query parameters string starts with a <c>'?'</c> character and
    /// contains a <c>`&amp;`</c> separated list of encoded (key, value) pairs
    /// in the form <c>key=value</c> after escaping the <c>key</c> and the
    /// <c>value</c>.
    /// </para>
    /// <para>
    /// This method assumes that <c>key</c> and <c>value</c> strings in any of
    /// the <paramref name="parameters" /> entries have no escape sequences in
    /// it. It converts all characters except for
    /// <see href="https://datatracker.ietf.org/doc/html/rfc3986">RFC 3986</see>
    /// unreserved
    /// characters to their hexadecimal representation.
    /// </para>
    /// </remarks>
    /// <param name="parameters">The query params to serialize.</param>
    /// <returns>
    /// The serialized string representation of the query params.
    /// </returns>
    private static string SerializeQueryParams(IParameters parameters)
    {
        var serialized = string.Join(
            '&',
            parameters.Select(parameter => parameter.ToString()));

        return serialized.Length > 0 ? string.Concat('?', serialized) : string.Empty;
    }
}
