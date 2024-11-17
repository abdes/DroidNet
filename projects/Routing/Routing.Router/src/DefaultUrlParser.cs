// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Routing.Detail;

namespace DroidNet.Routing;

/// <summary>
/// Provides the default implementation for parsing URLs into URL trees within the routing system.
/// </summary>
/// <remarks>
/// The <see cref="DefaultUrlParser"/> class is responsible for converting routing URL strings into structured <see cref="IUrlTree"/> objects.
/// It handles various URL components, including path segments, query
/// parameters, and matrix parameters, ensuring that the URL is correctly parsed
/// and validated.
/// <para>
/// Route URL strings are very similar to URIs specified in <em>RFC3986</em>.
/// They differ in some aspects that make them easier to work with for
/// specifying navigation routes. They don't use a scheme or authority (not
/// needed due to the exclusive use of these URIs within a navigation routing
/// context). They use parenthesis to specify secondary segments (e.g.,
/// '/inbox/33(popup:compose)'). They use colon syntax to specify router outlets
/// (e.g., the 'popup' in '/inbox/33(popup:compose)'). Finally, similarly to
/// URIs, they use the ';parameter=value' syntax (e.g., 'open=true' in
/// '/inbox/33;open=true/messages/44') to specify route-specific parameters.
/// </para>
/// <para>
/// Additionally, a path segment cannot be empty, unless the whole URL is just a root URL (i.e., "/"), it cannot be a single
/// dot '.' or a double dot unless it is a relative URL and the double dots occur at the start of the URL string.
/// </para>
/// </remarks>
/// <seealso href="https://en.wikipedia.org/wiki/Uniform_Resource_Identifier" />
public class DefaultUrlParser : IUrlParser
{
    /// <summary>
    /// Gets a value indicating whether multiple values for query parameters are allowed in URLs.
    /// </summary>
    /// <value>
    /// When set to <see langword="true"/>, multiple occurrences of the same query parameter key in the query parameters will get appended to existing values, separated by <c>','</c>.
    /// Otherwise, multiple occurrences will be considered as a malformed URL.
    /// </value>
    public bool AllowMultiValueParams { get; init; } = true;

    /// <summary>
    /// Attempts to parse the URL string into a <see cref="IUrlTree"/>.
    /// </summary>
    /// <param name="url">The URL string to parse.</param>
    /// <returns>The <see cref="UrlTree"/> representation of the URL.</returns>
    /// <exception cref="UriFormatException">Thrown if the URL string is malformed.</exception>
    /// <remarks>
    /// This method parses the provided URL string into a structured <see cref="IUrlTree"/> object.
    /// It processes the URL's path segments and query parameters, ensuring that the URL is fully consumed and correctly formatted.
    /// </remarks>
    /// <example>
    /// <strong>Example Usage</strong>
    /// <code><![CDATA[
    /// var parser = new DefaultUrlParser();
    /// var urlTree = parser.Parse("/inbox/33(popup:compose)?open=true");
    /// ]]></code>
    /// </example>
    public IUrlTree Parse(string url)
    {
        var remaining = url.AsSpan();
        try
        {
            var tree = new UrlTree(this.ParseRootSegment(ref remaining), this.ParseQueryParams(ref remaining))
            {
                IsRelative = !url.StartsWith('/'),
            };

            return !remaining.IsEmpty ? throw new UriFormatException("Expecting url to be fully consumed after parsing") : (IUrlTree)tree;
        }
        catch (Exception ex)
        {
            LogParsingError(url, remaining, ex);
            throw;
        }
    }

    /// <summary>
    /// Parses the root segment of a URL.
    /// </summary>
    /// <param name="remaining">
    /// When called, this parameter contains a read-only span of characters representing the input string to parse. It can only be
    /// manipulated using the defined <see cref="UrlParserHelpers">extension methods</see> for that purpose.
    /// </param>
    /// <returns>
    /// A <see cref="UrlSegmentGroup" /> that represents the parsed root segment of the URL. The <paramref name="remaining" />
    /// span is modified to consume whatever has been parsed and will only contain the remaining part of the URL.
    /// </returns>
    /// <remarks>
    /// This method consumes the leading slash of the URL if it exists. It always produces a root <see cref="UrlSegmentGroup" />
    /// with no segments and instead defers the segments parsing to its children, so they can be added with a corresponding
    /// outlet (even if it's just the primary outlet).
    /// </remarks>
    internal UrlSegmentGroup ParseRootSegment(ref ReadOnlySpan<char> remaining)
    {
        // If the URL is empty or just contains a query string, it should
        // produce a single segment group with no segments and no children.
        if (remaining.Length == 0 || remaining.PeekStartsWith('?'))
        {
            Debug.WriteLine("Empty URL -> single segment group with no segments and no children");
            return new UrlSegmentGroup([]);
        }

        var absolute = remaining.PeekStartsWith('/');
        return new UrlSegmentGroup(
            [],
            this.ParseChildren(ref remaining, absolute));
    }

    /// <summary>
    /// Parses child <see cref="UrlSegmentGroup" /> of a URL.
    /// </summary>
    /// <param name="remaining">
    /// When called, this parameter contains a read-only span of characters representing the input string to parse. It can only be
    /// manipulated using the defined <see cref="UrlParserHelpers">extension methods</see> for that purpose.
    /// </param>
    /// <param name="absolute">
    /// A boolean value indicating whether the URL is absolute. Defaults to false.
    /// </param>
    /// <returns>
    /// A dictionary of <see cref="UrlSegmentGroup" /> that represents the parsed child segment groups of the URL. The key of the
    /// dictionary is the outlet name and the value is the corresponding <see cref="UrlSegmentGroup" />.
    /// The <paramref name="remaining" /> span is modified to consume whatever has been parsed and will only contain the remaining
    /// part of the URL.
    /// </returns>
    /// <remarks>
    /// This method should not be called if the remaining URL is empty. It recursively parses the root segment group as
    /// well as child segment groups (inside parenthesis).
    /// </remarks>
    /// <exception cref="UriFormatException">If the remaining part of the URL is malformed.</exception>
    internal Dictionary<OutletName, IUrlSegmentGroup> ParseChildren(
        ref ReadOnlySpan<char> remaining,
        bool absolute = false)
    {
        Debug.Assert(remaining.Length > 0, $"don't call {nameof(this.ParseChildren)} if nothing is left in remaining");

        var allowDots = !absolute;

        var allowPrimary = remaining.PeekStartsWith("/(");

        _ = remaining.ConsumeOptional('/');

        List<UrlSegment> segments = [];
        if (!remaining.PeekStartsWith('('))
        {
            var segment = this.ParseSegment(ref remaining, allowDots);
            if (segment.Path.Length > 0)
            {
                segments.Add(segment);
            }
        }

        while (remaining.PeekStartsWith('/') && !remaining.PeekStartsWith("//") && !remaining.PeekStartsWith("/("))
        {
            remaining = remaining[1..]; // Skip the '/'
            var segment = this.ParseSegment(ref remaining, allowDots);
            segments.Add(segment);
            allowDots = string.Equals(segment.Path, "..", StringComparison.Ordinal);
        }

        // Eventually parse children inside (...)
        var children = new Dictionary<OutletName, IUrlSegmentGroup>();
        if (remaining.PeekStartsWith("/("))
        {
            remaining = remaining[1..]; // Skip the '/'
            children = this.ParseParens(allowPrimary: true, ref remaining);
        }

        Dictionary<OutletName, IUrlSegmentGroup> res = [];
        if (remaining.PeekStartsWith("("))
        {
            res = this.ParseParens(allowPrimary, ref remaining);
        }

        if (segments.Count > 0 || children.Count > 0)
        {
            res[OutletName.Primary] = new UrlSegmentGroup(segments, children);
        }

        return res;
    }

    /// <summary>
    /// Parses query string into a dictionary of key-value pairs.
    /// </summary>
    /// <param name="remaining">
    /// When called, this parameter contains a read-only span of characters representing the input string to parse. It
    /// can only be manipulated using the defined <see cref="UrlParserHelpers">extension methods</see> for that purpose.
    /// </param>
    /// <returns>
    /// A dictionary containing parsed query parameters. The <paramref name="remaining" /> span is modified to consume
    /// whatever has been parsed and will only contain the remaining part of the URL.
    /// </returns>
    internal IParameters ParseQueryParams(ref ReadOnlySpan<char> remaining)
    {
        var parameters = new Parameters();
        if (remaining.ConsumeOptional('?'))
        {
            do
            {
                this.ParseQueryParam(parameters, ref remaining);
            }
            while (remaining.ConsumeOptional('&'));
        }

        return parameters;
    }

    /// <summary>
    /// Parses a segment in the remaining path in a URI.
    /// </summary>
    /// <param name="remaining">
    /// When called, this parameter contains a read-only span of characters representing the input string to parse. It
    /// can only be manipulated using the defined <see cref="UrlParserHelpers">extension methods</see> for that purpose.
    /// </param>
    /// <param name="allowDots">
    /// A boolean value indicating whether dots ('.' or '..') are allowed to appear in the segment.
    /// </param>
    /// <returns>
    /// An instance of <see cref="UrlSegment" /> representing the parsed segment. The <paramref name="remaining" /> span
    /// is modified to consume whatever has been parsed and will only contain the remaining part of the URL.
    /// </returns>
    /// <exception cref="UriFormatException">
    /// Thrown if the segment is empty, or if it starts with a dot '..' and allowDots is false, or if it starts with '.'
    /// in any position.
    /// </exception>
    internal UrlSegment ParseSegment(ref ReadOnlySpan<char> remaining, bool allowDots)
    {
        var path = remaining.MatchSegment();

        if (path is "." || (!allowDots && path is ".."))
        {
            throw new UriFormatException(
                "Segment path cannot be '.' and cannot be '..' unless it is at the start of a relative path." +
                " Normalize the URL before parsing it or preferably do not use relative segments inside an absolute URL.");
        }

        remaining.Capture(path);
        return new UrlSegment(Uri.UnescapeDataString(path), this.ParseMatrixParams(ref remaining));
    }

    /// <summary>
    /// Parses the matrix parameters for a segment.
    /// </summary>
    /// <param name="remaining">
    /// When called, this parameter contains a read-only span of characters representing the input string to parse. It
    /// can only be manipulated using the defined <see cref="UrlParserHelpers">extension methods</see> for that purpose.
    /// </param>
    /// <returns>
    /// A dictionary containing parsed matrix parameters. The <paramref name="remaining" /> span is modified to consume
    /// whatever has been parsed and will only contain the remaining part of the URL.
    /// </returns>
    internal IParameters ParseMatrixParams(ref ReadOnlySpan<char> remaining)
    {
        var parameters = new Parameters();
        while (remaining.ConsumeOptional(';'))
        {
            this.ParseMatrixParam(parameters, ref remaining);
        }

        return parameters;
    }

    /// <summary>
    /// Logs an error that occurred during URL parsing, providing detailed information about the error's position and context.
    /// </summary>
    /// <param name="url">The URL that was being parsed when the error occurred.</param>
    /// <param name="remaining">The remaining span of characters that were not parsed.</param>
    /// <param name="exception">The exception that was thrown during parsing.</param>
    private static void LogParsingError(string url, ReadOnlySpan<char> remaining, Exception exception)
    {
        var position = remaining.Length == 0 ? url.Length : url.IndexOf(remaining.ToString(), StringComparison.Ordinal);
        var indent = new string(' ', position);

        // TODO: use a logger and log a message
        FormattableString str = $"""
                                 An error occurred while parsing url at position ({position})
                                 {url}
                                 {indent}^
                                 {indent}{exception.Message}.

                                 {exception.StackTrace}
                                 """;
        Debug.WriteLine(str);
    }

    /// <summary>
    /// Adds a parameter to the specified <see cref="Parameters"/> collection, handling decoding and multi-value parameters.
    /// </summary>
    /// <param name="parameters">The collection of parameters to which the key-value pair should be added.</param>
    /// <param name="key">The key of the parameter to add.</param>
    /// <param name="value">The value of the parameter to add. This value may be <see langword="null"/>.</param>
    /// <remarks>
    /// This method adds a key-value pair to the specified <see cref="Parameters"/> collection. It first decodes the key and value
    /// using <see cref="Uri.UnescapeDataString(string)"/>. If the key already exists in the collection and multi-value parameters
    /// are allowed, the new value is appended to the existing value, separated by a comma. If multi-value parameters are not
    /// allowed and the key already exists, an <see cref="InvalidOperationException"/> is thrown.
    /// </remarks>
    /// <exception cref="InvalidOperationException">
    /// Thrown if multi-value parameters are not allowed and the key already exists in the collection.
    /// </exception>
    /// <example>
    /// <strong>Example Usage</strong>
    /// <code><![CDATA[
    /// var parameters = new Parameters();
    /// AddParameter(parameters, "key", "value");
    /// AddParameter(parameters, "key", "anotherValue"); // Appends if multi-value parameters are allowed
    /// ]]></code>
    /// </example>
    private void AddParameter(Parameters parameters, string key, string? value)
    {
        var decodedKey = Uri.UnescapeDataString(key);
        var decodedValue = value;
        if (decodedValue is not null)
        {
            decodedValue = Uri.UnescapeDataString(decodedValue);
        }

        if (parameters.TryGetValue(decodedKey, out var existingValue))
        {
            if (!this.AllowMultiValueParams)
            {
                throw new InvalidOperationException("Multi value parameters are not allowed");
            }

            // Append to existing values
            decodedValue = string.Concat(existingValue, ',', decodedValue);
        }

        parameters.AddOrUpdate(decodedKey, decodedValue);
    }

    /// <summary>
    /// Parses a group of URL segments enclosed in parentheses, handling both primary and named outlets.
    /// </summary>
    /// <param name="allowPrimary">A value indicating whether a primary outlet is allowed. If <see langword="true"/>, the method allows primary outlet segments.</param>
    /// <param name="remaining">The remaining span of characters to parse. This span is modified as segments are consumed.</param>
    /// <returns>A dictionary mapping <see cref="OutletName"/> to <see cref="IUrlSegmentGroup"/> instances representing the parsed segments.</returns>
    /// <remarks>
    /// This method parses a group of URL segments enclosed in parentheses, which may include both primary and named outlets. It
    /// captures the opening parenthesis and processes each segment within the group, identifying outlet names and their
    /// corresponding segments.
    /// <para>
    /// If a segment includes a colon (':'), it is treated as a named outlet, and the method captures the outlet name and its
    /// segments. If no colon is found and primary outlets are allowed, the segment is treated as a primary outlet. Otherwise, an
    /// exception is thrown.
    /// </para>
    /// <para>
    /// The method continues parsing until it encounters a closing parenthesis or the end of the remaining span.
    /// If the group is not properly closed, a <see cref="UriFormatException"/> is thrown.
    /// </para>
    /// </remarks>
    /// <exception cref="UriFormatException">
    /// Thrown if the group is not properly closed or if a primary outlet is not allowed but encountered.
    /// </exception>
    /// <example>
    /// <strong>Example Usage</strong>
    /// <code><![CDATA[
    /// var parser = new DefaultUrlParser();
    /// var remaining = "(popup:compose//sidebar:details)".AsSpan();
    /// var segmentGroups = parser.ParseParens(true, ref remaining);
    /// ]]></code>
    /// </example>
    private Dictionary<OutletName, IUrlSegmentGroup> ParseParens(bool allowPrimary, ref ReadOnlySpan<char> remaining)
    {
        var segmentGroups = new Dictionary<OutletName, IUrlSegmentGroup>();
        remaining.Capture('(');
        if (remaining.Length == 0)
        {
            throw new UriFormatException("group was not closed");
        }

        while (!remaining.ConsumeOptional(')') && remaining.Length > 0)
        {
            var path = remaining.MatchSegment();

            string outletName;
            if (path.IndexOf(':', StringComparison.Ordinal) > -1)
            {
                outletName = path[..path.IndexOf(':', StringComparison.Ordinal)];
                remaining.Capture(outletName);
                remaining.Capture(':');
            }
            else
            {
                outletName = allowPrimary
                    ? (string)OutletName.Primary
                    : throw new UriFormatException(
                                    "not expecting a primary outlet child and did not find an outlet name followed by ':'");
            }

            var children = this.ParseChildren(ref remaining);

            segmentGroups[outletName]
                = children.Count == 1 ? children[OutletName.Primary] : new UrlSegmentGroup([], children);

            _ = remaining.ConsumeOptional("//");
        }

        return segmentGroups;
    }

    /// <summary>
    /// Parses a matrix parameter from the remaining URL span and adds it to the specified <see cref="Parameters"/> collection.
    /// </summary>
    /// <param name="parameters">The collection of parameters to which the matrix parameter should be added.</param>
    /// <param name="remaining">The remaining span of characters to parse. This span is modified as the parameter is consumed.</param>
    /// <remarks>
    /// This method extracts a matrix parameter from the remaining URL span and adds it to the specified <see cref="Parameters"/>
    /// collection. It delegates the actual parsing logic to the <see cref="ParseParam"/> method, specifying
    /// <see cref="ParamType.Matrix"/> as the parameter type.
    /// </remarks>
    private void ParseMatrixParam(Parameters parameters, ref ReadOnlySpan<char> remaining)
        => this.ParseParam(ParamType.Matrix, parameters, ref remaining);

    /// <summary>
    /// Parses a query parameter from the remaining URL span and adds it to the specified <see cref="Parameters"/> collection.
    /// </summary>
    /// <param name="parameters">The collection of parameters to which the query parameter should be added.</param>
    /// <param name="remaining">The remaining span of characters to parse. This span is modified as the parameter is consumed.</param>
    /// <remarks>
    /// This method extracts a query parameter from the remaining URL span and adds it to the specified <see cref="Parameters"/>
    /// collection. It delegates the actual parsing logic to the <see cref="ParseParam"/> method, specifying
    /// <see cref="ParamType.Query"/> as the parameter type.
    /// </remarks>
    private void ParseQueryParam(Parameters parameters, ref ReadOnlySpan<char> remaining)
        => this.ParseParam(ParamType.Query, parameters, ref remaining);

    /// <summary>
    /// Parses a parameter from the remaining URL span and adds it to the specified <see cref="Parameters"/> collection.
    /// </summary>
    /// <param name="paramType">The type of parameter to parse (matrix or query).</param>
    /// <param name="parameters">The collection of parameters to which the parsed parameter should be added.</param>
    /// <param name="remaining">The remaining span of characters to parse. This span is modified as the parameter is consumed.</param>
    /// <remarks>
    /// This method extracts a parameter from the remaining URL span based on the specified parameter type and adds it to the
    /// specified <see cref="Parameters"/> collection. It handles both matrix and query parameters, decoding the key and value,
    /// and ensuring that the parameter is correctly formatted and added to the collection.
    /// <para>
    /// If the key already exists in the collection and multi-value parameters are allowed, the new value is appended to the
    /// existing value, separated by a comma. If multi-value parameters are not allowed and the key already exists, an
    /// <see cref="InvalidOperationException"/> is thrown.
    /// </para>
    /// </remarks>
    /// <exception cref="InvalidEnumArgumentException">
    /// Thrown if an invalid <see cref="ParamType"/> is specified.
    /// </exception>
    /// <exception cref="InvalidOperationException">
    /// Thrown if multi-value parameters are not allowed and the key already exists in the collection.
    /// </exception>
    /// <example>
    /// <strong>Example Usage</strong>
    /// <code><![CDATA[
    /// var parameters = new Parameters();
    /// var remaining = "key=value".AsSpan();
    /// parser.ParseParam(ParamType.Query, parameters, ref remaining);
    /// ]]></code>
    /// </example>
    private void ParseParam(
        ParamType paramType,
        Parameters parameters,
        ref ReadOnlySpan<char> remaining)
    {
        var key = paramType switch
        {
            ParamType.Matrix => remaining.MatchMatrixParamKey(),
            ParamType.Query => remaining.MatchQueryParamKey(),
            _ => throw new InvalidEnumArgumentException(nameof(paramType), (int)paramType, typeof(ParamType)),
        };

        if (key.Length == 0)
        {
            return;
        }

        remaining.Capture(key);

        string? value = null;
        if (remaining.ConsumeOptional('='))
        {
            var valueMatch = paramType switch
            {
                ParamType.Matrix => remaining.MatchMatrixParamValue(),
                ParamType.Query => remaining.MatchQueryParamValue(),
                _ => throw new InvalidEnumArgumentException(nameof(paramType), (int)paramType, typeof(ParamType)),
            };
            if (valueMatch.Length != 0)
            {
                value = valueMatch;
                remaining.Capture(value);
            }
        }

        this.AddParameter(parameters, key, value);
    }
}
