// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System;
using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Routing.Detail;

/// <summary>
/// The default implementation of routes URL parsing, heavily inspired by the
/// way <see href="https://angular.dev/guide/routing">Angular Routing</see>
/// works.
/// </summary>
/// <remarks>
/// Route URL strings are very similar to URIs specified in
/// <see href="https://en.wikipedia.org/wiki/Uniform_Resource_Identifier">RFC3986</see>
/// .
/// They differ in some aspects that make them easier to work with for/edit
/// specifying navigation routes:
/// <list type="bullet">
/// <item>
/// They don't use a scheme or authority (not needed due to the exclusive use
/// of these URIs within a navigation routing context).
/// </item>
/// <item>
/// They use parenthesis to specify secondary segments (e.g.
/// '/inbox/33(popup:compose)').
/// </item>
/// <item>
/// They use colon syntax to specify router outlets (e.g. the 'popup' in
/// '/inbox/33(popup:compose)').
/// </item>
/// <item>
/// They use the ';parameter=value' syntax (e.g., 'open=true' in
/// '/inbox/33;open=true/messages/44') to specify route specific parameters.
/// </item>
/// </list>
/// <para>
/// A path segment cannot be empty, unless the whole url is just a root url
/// (i.e "/").
/// </para>
/// <para>
/// A path segment cannot be a single dot '.' or a double dot unless it is
/// a relative URL and the double dots occur at the start of the URL string.
/// </para>
/// </remarks>
public class DefaultUrlParser : IUrlParser
{
    /// <summary>
    /// Gets a value indicating whether multiple values for query parameters
    /// are allowed in URLs or not.
    /// </summary>
    /// <value>
    /// When set to <see langword="true" />, multiple occurrences of the same query
    /// parameter key in the query parameters will get appended to existing
    /// values, separated by <c>','</c>. Otherwise, multiple occurrences will
    /// be considered as a malformed URL.
    /// </value>
    public bool AllowMultiValueParams { get; init; } = true;

    /// <summary>
    /// Attempt to parse the URL string into a <see cref="UrlTree" />.
    /// </summary>
    /// <param name="url">The URL string to parse.</param>
    /// <returns>The <see cref="UrlTree" /> representation.</returns>
    /// <exception cref="UriFormatException">if the URL string is malformed.</exception>
    public IUrlTree Parse(string url)
    {
        var remaining = url.AsSpan();
        try
        {
            var tree = new UrlTree(this.ParseRootSegment(ref remaining), this.ParseQueryParams(ref remaining))
            {
                IsRelative = !url.StartsWith('/'),
            };

            if (!remaining.IsEmpty)
            {
                throw new UriFormatException("Expecting url to be fully consumed after parsing");
            }

            return tree;
        }
        catch (Exception ex)
        {
            LogParsingError(url, remaining, ex);
            throw;
        }
    }

    /// <summary>Parses the root segment of a URL.</summary>
    /// <param name="remaining">
    /// When called, this parameter contains a
    /// read-only span of characters representing the input string to parse.
    /// After being called, the span is modified to consume whatever has been
    /// parsed and will only contain the remaining part of the URL.
    /// </param>
    /// <returns>
    /// A <see cref="UrlSegmentGroup" /> that represents the parsed root
    /// segment of the URL.
    /// </returns>
    /// <remarks>
    /// This method consumes the leading slash of the URL if it exists. It
    /// always produces a root <see cref="UrlSegmentGroup" /> with no segments
    /// and instead defers the segments parsing to its children, so they can be
    /// added with a corresponding outlet (even if it's just the primary
    /// outlet).
    /// </remarks>
    internal UrlSegmentGroup ParseRootSegment(ref ReadOnlySpan<char> remaining)
    {
        // If the URL is empty or just contains a query string, it should
        // produce a single segment group with no segments and no children.
        if (remaining.Length == 0 || remaining.PeekStartsWith('?'))
        {
            return new UrlSegmentGroup([]);
        }

        var absolute = remaining.PeekStartsWith('/');
        var root = this.ParseChild(ref remaining, absolute);
        return new UrlSegmentGroup(
            [],
            new Dictionary<OutletName, IUrlSegmentGroup>(
                [new KeyValuePair<OutletName, IUrlSegmentGroup>(OutletName.Primary, root)]));
    }

    /// <summary>
    /// Parses child <see cref="UrlSegmentGroup" /> of a
    /// URL.
    /// </summary>
    /// <param name="remaining">
    /// When called, this parameter contains a
    /// read-only span of characters representing the input string to parse.
    /// After being called, the span is modified to consume whatever has been
    /// parsed and will only contain the remaining part of the URL.
    /// </param>
    /// <param name="absolute">
    /// A boolean value indicating whether the URL is absolute. Defaults to
    /// false.
    /// </param>
    /// <returns>
    /// A dictionary of <see cref="UrlSegmentGroup" /> that represents the
    /// parsed child segment groups of the URL. The key of the dictionary is
    /// the outlet name and the value is the corresponding
    /// <see cref="UrlSegmentGroup" />.
    /// </returns>
    /// <remarks>
    /// This method should not be called if the remaining URL is empty. It
    /// recursively parses the root segment group as well as child segment
    /// groups (inside parenthesis).
    /// </remarks>
    /// <exception cref="UriFormatException">If the remaining part of the URL is malformed.</exception>
    internal UrlSegmentGroup ParseChild(ref ReadOnlySpan<char> remaining, bool absolute = false)
    {
        Debug.Assert(remaining.Length > 0, $"don't call {nameof(this.ParseChild)} if nothing is left in remaining");

        List<UrlSegment> segments = []
            ;

        var allowPrimary = false;
        var allowDots = !absolute;

        // Parse normal segments. At this point, we don't want to see a '/' at the beginning of the path anymore. Even
        // the url was absolute, just remove the leading '/'.
        if (!absolute)
        {
            if (remaining.PeekStartsWith('/'))
            {
                throw new UriFormatException("a relative path cannot start with a '/'");
            }

            var segment = this.ParseSegment(ref remaining, allowDots);
            segments.Add(segment);
            allowDots = string.Equals(segment.Path, "..", StringComparison.Ordinal);
        }

        while (remaining.PeekStartsWith('/') && !remaining.PeekStartsWith("~~"))
        {
            remaining = remaining[1..]; // Skip the '/'
            var segment = this.ParseSegment(ref remaining, allowDots);
            segments.Add(segment);
            allowDots = string.Equals(segment.Path, "..", StringComparison.Ordinal);
        }

        Debug.Assert(segments.Count != 0, "we should have at least one segment with empty path");

        // Eventually parse children inside (...)
        var children = new Dictionary<OutletName, IUrlSegmentGroup>();
        if (remaining.PeekStartsWith("("))
        {
            if (segments[^1].Path.Length == 0)
            {
                allowPrimary = true;
            }

            children = this.ParseParens(allowPrimary, ref remaining);
        }

        return new UrlSegmentGroup(segments, children);
    }

    /// <summary>
    /// Parses query string into a dictionary of key-value pairs.
    /// </summary>
    /// <param name="remaining">
    /// When called, this parameter contains a
    /// read-only span of characters representing the input string to parse.
    /// After being called, the span is modified to consume whatever has been
    /// parsed and will only contain the remaining part of the URL.
    /// </param>
    /// <returns>A dictionary containing parsed query parameters.</returns>
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

    /// <summary>Parses a segment in the remaining path in a URI.</summary>
    /// <param name="remaining">
    /// When called, this parameter contains a read-only span of characters
    /// representing the input string to parse. After being called, the span is
    /// modified to consume whatever has been parsed and will only contain the
    /// remaining part of the URL.
    /// </param>
    /// <param name="allowDots">
    /// A boolean value indicating whether dots ('.' or '..') are allowed to
    /// appear in the segment.
    /// </param>
    /// <returns>
    /// An instance of <see cref="UrlSegment" /> representing the parsed segment.
    /// </returns>
    /// <exception cref="UriFormatException">
    /// Thrown if the segment is empty, or if it starts with a dot '..' and
    /// allowDots is false, or if it starts with '.' in any position.
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
    /// When called, this parameter contains a read-only span of characters
    /// representing the input string to parse. After being called, the span is
    /// modified to consume whatever has been parsed and will only contain the
    /// remaining part of the URL.
    /// </param>
    /// <returns>A dictionary containing parsed matrix parameters.</returns>
    internal IParameters ParseMatrixParams(ref ReadOnlySpan<char> remaining)
    {
        var parameters = new Parameters();
        while (remaining.ConsumeOptional(';'))
        {
            this.ParseMatrixParam(parameters, ref remaining);
        }

        return parameters;
    }

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

    private Dictionary<OutletName, IUrlSegmentGroup> ParseParens(bool allowPrimary, ref ReadOnlySpan<char> remaining)
    {
        var segmentGroups = new Dictionary<OutletName, IUrlSegmentGroup>();
        remaining.Capture('(');

        var closed = false;
        while (!closed && remaining.Length > 0)
        {
            var path = remaining.MatchSegment();

            string outletName;
            if (path.IndexOf(':', StringComparison.Ordinal) > -1)
            {
                outletName = path[..path.IndexOf(':', StringComparison.Ordinal)];
                remaining.Capture(outletName);
                remaining.Capture(':');
            }
            else if (allowPrimary)
            {
                outletName = OutletName.Primary;
                remaining.Capture(outletName);
            }
            else
            {
                throw new UriFormatException("not expecting a primary outlet child and did not find an outlet name");
            }

            segmentGroups[outletName] = this.ParseChild(ref remaining);

            if (remaining.Length == 0)
            {
                break;
            }

            var next = remaining[0];
            if (next is not '/' and not ')' and not ';' and not '(' and not '~')
            {
                throw new UriFormatException(
                    $"Expecting one of '/', ')', ';', '(', '~' after a segment but got '{next}'");
            }

            closed = remaining.ConsumeOptional(')');
            if (!closed)
            {
                _ = remaining.ConsumeOptional("~~");
            }
        }

        if (!closed)
        {
            throw new UriFormatException("Parenthesis not closed");
        }

        return segmentGroups;
    }

    private void ParseMatrixParam(Parameters parameters, ref ReadOnlySpan<char> remaining)
        => this.ParseParam(ParamType.Matrix, parameters, ref remaining);

    private void ParseQueryParam(Parameters parameters, ref ReadOnlySpan<char> remaining)
        => this.ParseParam(ParamType.Query, parameters, ref remaining);

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
