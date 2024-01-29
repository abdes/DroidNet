// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using FluentAssertions;
using FluentAssertions.Execution;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Unit test cases for the <see cref="UrlSegment" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class UrlSegmentTests
{
    internal static IEnumerable<object[]> PathData
        => new[]
        {
            new object[] { "home", "home" },
            [
                string.Empty,
                string.Empty,
            ],
            [
                "a;b",
                "a%3Bb",
            ],
            [
                "a b",
                "a%20b",
            ],
            [
                "a%20b",
                "a%2520b",
            ],
        };

    internal static IEnumerable<object[]> ParametersData
        => new[]
        {
            new object[] { new Dictionary<string, string>(), string.Empty },
            [
                new Dictionary<string,
                    string>()
                {
                    { "1", "one" },
                },
                ";1=one",
            ],
            [
                new Dictionary<string,
                    string>()
                {
                    { "foo", "foo-value" },
                    { "bar", "baz=" },
                    { "sp ace", "v a lue" },
                    { "empty", string.Empty },
                },
                ";foo=foo-value;bar=baz%3D;sp%20ace=v%20a%20lue;empty=",
            ],
        };

    internal static IEnumerable<object?[]> SegmentData => new[]
    {
        new object?[]
        {
            // The path
            "home",

            // The dictionary of parameters
            null,

            // The expected string
            "home",

            // The test case custom display name
            "Just a path",
        },
        [
            "new home",
            null,
            "new%20home",
            "Just a path with ' ' % encoded",
        ],
        [
            ";-foo",
            null,
            "%3B-foo",
            "Just a path with ';' % encoded",
        ],
        [
            "home",
            new Dictionary<string, string>
            {
                { "name", "Alice" },
            },
            "home;name=Alice",
            "Path and one parameter",
        ],
        [
            "home",
            new Dictionary<string, string>
            {
                { "the name", "Alice The Magnificent" },
            },
            "home;the%20name=Alice%20The%20Magnificent",
            "Path and 1 parameter, % encoding",
        ],
        [
            "home",
            new Dictionary<string, string>
            {
                { "id", "42" },
                { "name", "Alice" },
            },
            "home;id=42;name=Alice",
            "Path and 2 parameters, % encoding",
        ],
    };

    /// <summary>
    /// Generate a custom display name for the test cases in <see cref="Serialize" />
    /// dynamic data test.
    /// </summary>
    /// <param name="methodInfo">The test method info.</param>
    /// <param name="data">The current row of test data.</param>
    /// <returns>The custom display name to be used.</returns>
    public static string GetCustomDynamicDataDisplayName(MethodInfo methodInfo, object?[] data)
        => $"{methodInfo.Name} - {data[3]}";

    /// <summary>Test the constructor with an empty path.</summary>
    [TestMethod]
    public void Constructor_WithEmptyPath_ThrowsArgumentException()
    {
        var action = () => _ = new UrlSegment(string.Empty);

        _ = action.Should().NotThrow("Segment path can be empty.");
    }

    /// <summary>Test the constructor with an invalid parameter name.</summary>
    [TestMethod]
    public void Constructor_WithEmptyParameterName_ThrowsArgumentException()
    {
        Dictionary<string, string?> parameters = new() { { string.Empty, "value" } };

        var action = () => _ = new UrlSegment("segment", parameters);

        _ = action.Should().Throw<ArgumentException>("Segment path cannot be empty.");
    }

    /// <summary>Test Adding a parameter to a <see cref="UrlSegment" />.</summary>
    [TestMethod]
    public void AddParameter_WithValidParameterName_Works()
    {
        Dictionary<string, string?> parameters = new() { { "param", "value" } };
        var segment = new UrlSegment("segment", parameters);

        segment.AddParameter("foo", "bar");

        _ = segment.Parameters.Should().ContainKey("foo").WhoseValue.Should().Be("bar");
    }

    /// <summary>Test Adding a parameter to a <see cref="UrlSegment" />.</summary>
    [TestMethod]
    public void AddParameter_WithEmptyParameterName_ThrowsArgumentException()
    {
        Dictionary<string, string?> parameters = new() { { string.Empty, "value" } };

        var action = () => _ = new UrlSegment("segment", parameters);

        _ = action.Should().Throw<ArgumentException>("Parameter name cannot be empty.");
    }

    /// <summary>Test Adding a parameter to a <see cref="UrlSegment" />.</summary>
    [TestMethod]
    public void AddParameter_WithExistingParameterName_ThrowsArgumentException()
    {
        Dictionary<string, string?> parameters = new() { { "param", "value" } };
        var segment = new UrlSegment("segment", parameters);

        var action = () => segment.AddParameter("param", "different-value");

        _ = action.Should().Throw<ArgumentException>("A parameter with the same name already exists.");
    }

    /// <summary>
    /// Test the constructor with a valid path and no parameters.
    /// </summary>
    [TestMethod]
    public void Constructor_WithValidPathAndNoParameters()
    {
        var path = "home";

        var segment = new UrlSegment(path);

        _ = segment.Path.Should().Be(path);
        using (new AssertionScope())
        {
            _ = segment.Parameters.Should().NotBeNull();
            _ = segment.Parameters.Should().BeEmpty();
        }
    }

    /// <summary>
    /// Test the constructor with a valid path and some parameters.
    /// </summary>
    [TestMethod]
    public void Constructor_WithValidPathAndSomeParameters()
    {
        var path = "user";
        Dictionary<string, string?> parameters = new()
        {
            { "id", "42" },
            { "name", "Alice" },
        };

        var segment = new UrlSegment(path, parameters);

        _ = segment.Path.Should().Be(path);
        _ = segment.Parameters.Should().BeEquivalentTo(parameters);
    }

    /// <summary>
    /// A parametrized test for serialization of <see cref="UrlSegment" /> into
    /// a string.
    /// </summary>
    /// <param name="path">The path.</param>
    /// <param name="parameters">The dictionary of matrix parameters.</param>
    /// <param name="expected">The expected result.</param>
    /// <param name="displayName">Not used.</param>
    [TestMethod]
    [DynamicData(nameof(SegmentData), DynamicDataDisplayName = nameof(GetCustomDynamicDataDisplayName))]
    public void Serialize(string path, Dictionary<string, string?> parameters, string expected, string displayName)
    {
        _ = displayName;
        var segment = new UrlSegment(path, parameters);

        var serialized = segment.ToString();

        _ = serialized.Should().Be(expected);
    }

    /// <summary>
    /// A data-driven test for <see cref="UrlSegment.SerializePath" />.
    /// </summary>
    /// <param name="path">The path string to serialize.</param>
    /// <param name="expected">The expected result string.</param>
    [TestMethod]
    [DynamicData(nameof(PathData))]
    public void SerializePath(string path, string expected)
    {
        var result = UrlSegment.SerializePath(path);
        _ = result.Should().Be(expected);
    }

    /// <summary>
    /// A data-driven test for <see cref="UrlSegment.SerializeMatrixParams" />.
    /// </summary>
    /// <param name="parameters">The dictionary of parameters to serialize.</param>
    /// <param name="expected">The expected result string.</param>
    [TestMethod]
    [DynamicData(nameof(ParametersData))]
    public void SerializeParameters(IDictionary<string, string?> parameters, string expected)
    {
        var result = UrlSegment.SerializeMatrixParams(parameters);
        _ = result.Should().Be(expected);
    }
}
