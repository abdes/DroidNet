// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Tests;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing.Detail;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>Unit tests for the <see cref="DefaultUrlParser" /> class.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("URL Parsing")]
public class DefaultUrlParserTests
{
    [TestMethod]
    public void EmptyUrl_NoSegmentsNoChildren()
    {
        const string url = "";
        var parser = new DefaultUrlParser();

        var tree = parser.Parse(url);

        tree.IsRelative.Should().BeTrue();
        tree.QueryParams.Should().BeEmpty();
        tree.Root.Segments.Should().BeEmpty();
        tree.Root.Children.Should().BeEmpty();
        tree.Root.Parent.Should().BeNull();

        var serialized = tree.ToString();
        serialized.Should().Be(url);
    }

    [TestMethod]
    public void RootUrl_OneChildForPrimaryOutletWithEmptyPathSegment()
    {
        const string url = "/";
        var parser = new DefaultUrlParser();

        var tree = parser.Parse(url);

        tree.IsRelative.Should().BeFalse();
        tree.QueryParams.Should().BeEmpty();
        tree.Root.Parent.Should().BeNull();
        tree.Root.Segments.Should().BeEmpty();
        tree.Root.Children.Count.Should().Be(1);
        tree.Root.Children.Should().ContainKey(OutletName.Primary);
        var primary = tree.Root.Children[OutletName.Primary];
        primary.Segments.Should().HaveCount(1).And.Contain(s => s.Path == string.Empty && s.Parameters.IsEmpty);

        var serialized = tree.ToString();
        serialized.Should().Be(url);
    }

    [TestMethod]
    public void RootUrl_SinglePathSegment_OneChildForPrimaryOutletWithPathSegment()
    {
        const string url = "/home";
        var parser = new DefaultUrlParser();

        var tree = parser.Parse(url);

        tree.IsRelative.Should().BeFalse();
        tree.QueryParams.Should().BeEmpty();
        tree.Root.Parent.Should().BeNull();
        tree.Root.Segments.Should().BeEmpty();
        tree.Root.Children.Count.Should().Be(1);
        tree.Root.Children.Should().ContainKey(OutletName.Primary);
        var primary = tree.Root.Children[OutletName.Primary];
        primary.Segments.Should().HaveCount(1).And.Contain(s => s.Path == "home" && s.Parameters.IsEmpty);

        var serialized = tree.ToString();
        serialized.Should().Be(url);
    }

    [TestMethod]
    public void RootUrl_MultiSegmentPath_NoOutlet_OneChildForPrimaryOutletWithMultiSegment()
    {
        const string url = "/foo/bar";
        var parser = new DefaultUrlParser();

        var tree = parser.Parse(url);

        tree.IsRelative.Should().BeFalse();
        tree.QueryParams.Should().BeEmpty();
        tree.Root.Parent.Should().BeNull();
        tree.Root.Segments.Should().BeEmpty();
        tree.Root.Children.Count.Should().Be(1);
        tree.Root.Children.Should().ContainKey(OutletName.Primary);
        var primary = tree.Root.Children[OutletName.Primary];
        primary.Segments.Should().HaveCount(2);
        primary.Segments[0].Should().BeEquivalentTo(new UrlSegment("foo"));
        primary.Segments[1].Should().BeEquivalentTo(new UrlSegment("bar"));

        var serialized = tree.ToString();
        serialized.Should().Be(url);
    }

    [TestMethod]
    [DataRow("", "foo")]
    [DataRow("foo", "bar")]
    public void RootUrl_NoSegments_Outlet_SingleChildForOutlet(string outlet, string outletSegment)
    {
        var url = outlet.Length == 0 ? $"/({outletSegment})" : $"/({outlet}:{outletSegment})";
        var parser = new DefaultUrlParser();

        var tree = parser.Parse(url);

        tree.IsRelative.Should().BeFalse();
        tree.QueryParams.Should().BeEmpty();
        tree.Root.Parent.Should().BeNull();
        tree.Root.Segments.Should().BeEmpty();
        tree.Root.Children.Count.Should().Be(1);

        tree.Root.Children.Should().HaveCount(1).And.ContainKey(outlet);
        var outletChild = tree.Root.Children[outlet];
        outletChild.Segments.Should().HaveCount(1).And.Contain(s => s.Path == outletSegment);

        var serialized = tree.ToString();
        serialized.Should().Be(outlet.Length == 0 ? $"/{outletSegment}" : $"/({outlet}:{outletSegment})");
    }

    [TestMethod]
    public void RootUrl_Segment_PrimaryOutlet_ChildWithChild()
    {
        const string segment = "foo";
        const string outletPath = "bar";
        const string url = $"/foo/({outletPath})";
        var parser = new DefaultUrlParser();

        var tree = parser.Parse(url);

        tree.IsRelative.Should().BeFalse();
        tree.QueryParams.Should().BeEmpty();
        tree.Root.Parent.Should().BeNull();
        tree.Root.Segments.Should().BeEmpty();
        tree.Root.Children.Count.Should().Be(1);

        tree.Root.Children.Should().HaveCount(1).And.ContainKey(OutletName.Primary);
        var rootChild = tree.Root.Children[OutletName.Primary];
        rootChild.Segments.Should().HaveCount(1).And.Contain(s => s.Path == segment);
        rootChild.Children.Should().HaveCount(1).And.ContainKey(OutletName.Primary);

        var outletChild = rootChild.Children[OutletName.Primary];
        outletChild.Segments.Should().HaveCount(1).And.Contain(s => s.Path == outletPath);

        var serialized = tree.ToString();
        serialized.Should().Be(url);
    }

    [TestMethod]
    public void TrailingSlash_ExtraEmptySegment()
    {
        const string url = "/foo/";
        var parser = new DefaultUrlParser();

        var tree = parser.Parse(url);

        tree.IsRelative.Should().BeFalse();
        tree.QueryParams.Should().BeEmpty();
        tree.Root.Parent.Should().BeNull();
        tree.Root.Segments.Should().BeEmpty();
        tree.Root.Children.Count.Should().Be(1);
        tree.Root.Children.Should().ContainKey(OutletName.Primary);
        var primary = tree.Root.Children[OutletName.Primary];
        primary.Segments.Should().HaveCount(2);
        primary.Segments[0].Should().BeEquivalentTo(new UrlSegment("foo"));
        primary.Segments[1].Should().BeEquivalentTo(new UrlSegment(string.Empty));

        var serialized = tree.ToString();
        serialized.Should().Be(url);
    }

    /// <summary>
    /// Tests that two-way serialization produces the original url string.
    /// </summary>
    /// <param name="url">The input url string.</param>
    /// <param name="canonical">
    /// The canonical equivalent of the URL that should be produce from
    /// serializing the parsed tree. If not provided, it is assumed that the
    /// url is already in its canonical form.
    /// </param>
    [TestMethod]
    [DataRow("/")]
    [DataRow("..")]
    [DataRow("../a")]
    [DataRow("../../a")]
    [DataRow("/a/")]
    [DataRow("/a/b/")]
    [DataRow("/teams")]
    [DataRow("/home/(welcome)")]
    [DataRow("/home(left:teams//right:../projects)")]
    [DataRow("/team/33")]
    [DataRow("/team/33(right:member)")]
    [DataRow("/team/33(right:projects/(13//popup:new))")]
    [DataRow("/team/33/members")]
    [DataRow("/team/33/members/12/(details(popup:new)//manager:../10)")]
    [DataRow("/(a:b)")]
    [DataRow("/(primary//a:b)", "/primary(a:b)")]
    [DataRow("/primary(left:a)")]
    [DataRow("/primary(left:a/b)")]
    [DataRow("/primary(left:a(sub:b))")]
    [DataRow("/primary(left:../(a//sub:b))")]
    [DataRow("/primary(left:a/(b//sub:b))")]
    [DataRow("/primary(left:a//right:b)")]
    [DataRow("/primary(left:a)")]
    [DataRow("/primary(left:a//right:b)")]
    [DataRow("/primary/(more//left:a//right:b)")]
    [DataRow("/(app:Home/Welcome//dock:left(1:One;pinned//2:Two;below=1//3:Three;pinned;above=2//4:Four))")]
    public void TwoWaySerialization(string url, string? canonical = null)
    {
        var parser = new DefaultUrlParser() { AllowMultiValueParams = true };

        var tree = parser.Parse(url);
        var serialized = tree.ToString();

        _ = serialized.Should().Be(canonical ?? url);
    }

    /// <summary>
    /// Tests Query Params parsing when the query string is empty.
    /// </summary>
    [TestMethod]
    public void ParseQueryParams_NoParams()
    {
        const string url = "/test";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the end of the string

        var result = parser.ParseQueryParams(ref remaining);

        _ = result.Should().BeEmpty("the URL did not have a query string");
    }

    /// <summary>
    /// Tests Query Params parsing when only one parameter is present.
    /// </summary>
    [TestMethod]
    public void ParseQueryParams_OneParam()
    {
        const string url = "/test?param=value";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the start of the query string

        var result = parser.ParseQueryParams(ref remaining);

        _ = result.Should().Contain(parameter => parameter.Name == "param" && parameter.Value == "value");
    }

    /// <summary>
    /// Tests Query Params parsing when multiple parameters are present.
    /// </summary>
    [TestMethod]
    public void ParseQueryParams_MultipleParams()
    {
        const string url = "/test?foo=bar&goo=baz";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the start of the query string

        var result = parser.ParseQueryParams(ref remaining);

        _ = result.Should().HaveCount(2);
        _ = result.Should().Contain(parameter => parameter.Name == "foo" && parameter.Value == "bar");
        _ = result.Should().Contain(parameter => parameter.Name == "goo" && parameter.Value == "baz");
    }

    /// <summary>
    /// Tests Query Params parsing when a parameter occurs multiple times and
    /// multi-value parameters are allowed.
    /// </summary>
    [TestMethod]
    public void ParseQueryParams_MultipleValues_Allowed_ReturnsCommaSeparatedValues()
    {
        const string url = "/test?foo=bar&foo=baz";
        var parser = new DefaultUrlParser() { AllowMultiValueParams = true };
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the start of the query string

        var result = parser.ParseQueryParams(ref remaining);

        _ = result.Should().HaveCount(1);
        _ = result.Should().Contain(parameter => parameter.Name == "foo" && parameter.Value == "bar,baz");
    }

    /// <summary>
    /// Tests Query Params parsing when a parameter occurs multiple times and
    /// multi-value parameters are not allowed.
    /// </summary>
    [TestMethod]
    public void ParseQueryParams_MultipleValues_NotAllowed_Throws()
    {
        const string url = "/test?foo=bar&foo=baz";
        var parser = new DefaultUrlParser() { AllowMultiValueParams = false };
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the start of the query string

        var action = () =>
        {
            var remaining = url.AsSpan();
            remaining.Capture("/test"); // Move to the start of the query string
            return parser.ParseQueryParams(ref remaining);
        };

        _ = action.Should().Throw<InvalidOperationException>();
    }

    /// <summary>
    /// Tests Query Params parsing when the query string has a parameter with
    /// an empty key.
    /// </summary>
    [TestMethod]
    public void ParseQueryParams_EmptyKey_NotConsumed()
    {
        const string url = "/test?=bar";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the start of the query string

        var result = parser.ParseQueryParams(ref remaining);

        _ = result.Should().BeEmpty("the URL did not have a query string");
        _ = remaining.ToString().Should().Be("=bar");
    }

    /// <summary>
    /// Tests Matrix Params parsing when the matrix string is empty.
    /// </summary>
    [TestMethod]
    public void ParseMatrixParams_NoParams()
    {
        const string url = "/test";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the end of the string

        var result = parser.ParseMatrixParams(ref remaining);

        _ = result.Should().BeEmpty("the URL did not have a matrix string");
    }

    /// <summary>
    /// Tests Matrix Params parsing when only one parameter is present.
    /// </summary>
    [TestMethod]
    public void ParseMatrixParams_OneParam()
    {
        const string url = "/test;param=value1,value2/next";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the start of the matrix string

        var result = parser.ParseMatrixParams(ref remaining);

        _ = result.Should().Contain(parameter => parameter.Name == "param" && parameter.Value == "value1,value2");
    }

    /// <summary>
    /// Tests Matrix Params parsing when multiple parameters are present.
    /// </summary>
    [TestMethod]
    public void ParseMatrixParams_MultipleParams()
    {
        const string url = "/test;foo=bar;goo=baz";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the start of the matrix string

        var result = parser.ParseMatrixParams(ref remaining);

        _ = result.Should().HaveCount(2);
        _ = result.Should().Contain(parameter => parameter.Name == "foo" && parameter.Value == "bar");
        _ = result.Should().Contain(parameter => parameter.Name == "goo" && parameter.Value == "baz");
    }

    /// <summary>
    /// Tests Matrix Params parsing when a parameter occurs multiple times and
    /// multi-value parameters are allowed.
    /// </summary>
    [TestMethod]
    public void ParseMatrixParams_MultipleValues_Allowed_ReturnsCommaSeparatedValues()
    {
        const string url = "/test;foo=bar;foo=baz";
        var parser = new DefaultUrlParser() { AllowMultiValueParams = true };
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the start of the matrix string

        var result = parser.ParseMatrixParams(ref remaining);

        _ = result.Should().HaveCount(1);
        _ = result.Should().Contain(parameter => parameter.Name == "foo" && parameter.Value == "bar,baz");
    }

    /// <summary>
    /// Tests Matrix Params parsing when a parameter occurs multiple times and
    /// multi-value parameters are not allowed.
    /// </summary>
    [TestMethod]
    public void ParseMatrixParams_MultipleValues_NotAllowed_Throws()
    {
        const string url = "/test;foo=bar;foo=baz";
        var parser = new DefaultUrlParser() { AllowMultiValueParams = false };
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the start of the matrix string

        var action = () =>
        {
            var remaining = url.AsSpan();
            remaining.Capture("/test"); // Move to the start of the matrix string
            return parser.ParseMatrixParams(ref remaining);
        };

        _ = action.Should().Throw<InvalidOperationException>();
    }

    /// <summary>
    /// Tests Matrix Params parsing when the matrix string has a parameter with
    /// an empty key.
    /// </summary>
    [TestMethod]
    public void ParseMatrixParams_EmptyKey_NotConsumed()
    {
        const string url = "/test;=bar";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();
        remaining.Capture("/test"); // Move to the start of the matrix string

        var result = parser.ParseMatrixParams(ref remaining);

        _ = result.Should().BeEmpty("the URL did not have a matrix string");
        _ = remaining.ToString().Should().Be("=bar");
    }

    /// <summary>Tests empty segment parsing.</summary>
    [TestMethod]
    public void ParseSegment_Empty()
    {
        const string url = "";
        var parser = new DefaultUrlParser();

        var remaining = url.AsSpan();
        var result = parser.ParseSegment(ref remaining, allowDots: false);

        result.Path.Should().Be(string.Empty);
    }

    /// <summary>
    /// Tests that '.' segments are always rejected no matter whether <paramref name="allowDots" /> is true or false.
    /// </summary>
    /// <param name="allowDots">Whether to allow dot segments or not.</param>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public void ParseSegment_SingleDot_NotAllowed(bool allowDots)
    {
        const string url = ".";
        var parser = new DefaultUrlParser();

        var action = () =>
        {
            var remaining = url.AsSpan();
            return parser.ParseSegment(ref remaining, allowDots);
        };

        _ = action.Should().Throw<UriFormatException>();
    }

    /// <summary>Tests double dot segment parsing when allowDots is true.</summary>
    [TestMethod]
    public void ParseSegment_DoubleDot_AllowDotsTrue()
    {
        const string url = "..";
        var parser = new DefaultUrlParser();

        var remaining = url.AsSpan();
        var result = parser.ParseSegment(ref remaining, allowDots: true);

        _ = result.Path.Should().Be("..");
    }

    /// <summary>Tests double dot segment parsing when allowDots is false.</summary>
    [TestMethod]
    public void ParseSegment_DoubleDot_AllowDotsFalse()
    {
        const string url = "..";
        var parser = new DefaultUrlParser();

        var action = () =>
        {
            var remaining = url.AsSpan();
            return parser.ParseSegment(ref remaining, allowDots: false);
        };

        _ = action.Should().Throw<UriFormatException>();
    }

    /// <summary>Tests segment parsing with empty path.</summary>
    [TestMethod]
    public void ParseSegment_EmptyPath()
    {
        const string url = ";param=value";
        var parser = new DefaultUrlParser();

        var remaining = url.AsSpan();
        var result = parser.ParseSegment(ref remaining, allowDots: false);

        _ = result.Path.Should().Be(string.Empty);

        _ = result.Parameters.Should()
            .HaveCount(1)
            .And.Contain(parameter => parameter.Name == "param" && parameter.Value == "value");
    }

    /// <summary>Tests segment parsing with no parameter value.</summary>
    [TestMethod]
    public void ParseSegment_ParamNoValue()
    {
        const string url = "path;param";
        var parser = new DefaultUrlParser();

        var remaining = url.AsSpan();
        var result = parser.ParseSegment(ref remaining, allowDots: false);

        _ = result.Path.Should().Be("path");
        _ = result.Parameters.Should()
            .HaveCount(1)
            .And.Contain(parameter => parameter.Name == "param" && parameter.Value == null);
    }

    /// <summary>Tests segment parsing, no parameters.</summary>
    [TestMethod]
    public void ParseSegment_NoParams()
    {
        const string url = "test";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();

        var result = parser.ParseSegment(ref remaining, allowDots: false);

        _ = result.Path.Should().Be("test");
        _ = result.Parameters.Should().BeEmpty();
    }

    /// <summary>Tests segment parsing with a path and parameter.</summary>
    [TestMethod]
    public void ParseSegment_WithParams()
    {
        const string url = "test;param=value";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();

        var result = parser.ParseSegment(ref remaining, allowDots: false);

        _ = result.Path.Should().Be("test");
        _ = result.Parameters.Should()
            .HaveCount(1)
            .And.Contain(parameter => parameter.Name == "param" && parameter.Value == "value");
    }

    /// <summary>Test root segment parsing when the input URL is empty.</summary>
    [TestMethod]
    public void ParseRootSegment_EmptyUrl()
    {
        const string url = "";
        var parser = new DefaultUrlParser();
        var remaining = url.AsSpan();

        var result = parser.ParseRootSegment(ref remaining);

        _ = result.Segments.Should().BeEmpty();
        _ = result.Children.Should().BeEmpty();
    }

    /// <summary>Test that parsing a malformed URL would throw an exception.</summary>
    /// <param name="url">The url string to parse.</param>
    [TestMethod]
    [DataRow("/../a")]
    [DataRow("/a/../a")]
    [DataRow("/./a")]
    [DataRow("/b/./a")]
    [DataRow("/xxx(")]
    [DataRow("/x#")]
    [DataRow("x??")]
    [DataRow("/xxx(()")]
    [DataRow("/xxx;=")]
    [DataRow("/xxx/?=x")]
    public void Parse_MalformedUrl_Throws(string url)
    {
        var parser = new DefaultUrlParser();

        var action = () => _ = parser.Parse(url);

        _ = action.Should().Throw<UriFormatException>();
    }
}
