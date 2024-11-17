// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Moq;

namespace DroidNet.Routing.Tests;

/// <summary>
/// Unit test cases for the <see cref="DefaultUrlSerializer" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("URL Serialization")]
public class DefaultUrlSerializerTests
{
    private DefaultUrlSerializer? serializer;
    private Mock<IUrlParser>? urlParserMock;

    /// <summary>Initialize the parser and serializer before each test.</summary>
    [TestInitialize]
    public void TestInitialize()
    {
        this.urlParserMock = new Mock<IUrlParser>();
        this.serializer = new DefaultUrlSerializer(this.urlParserMock.Object);
    }

    /// <summary>
    /// Test that the url serializer forwards the parsing to the url parser.
    /// </summary>
    [TestMethod]
    public void Parse_CallsUrlParser()
    {
        const string url = "/test";
        var expectedTree = new UrlTree();
        _ = this.urlParserMock!.Setup(p => p.Parse(url)).Returns(expectedTree);

        var result = this.serializer!.Parse(url);

        this.urlParserMock.Verify(p => p.Parse(url), Times.Once);
        _ = result.Should().BeSameAs(expectedTree);
    }

    /// <summary>
    /// Test that the serializer produces the same URL string when serializing
    /// a tree produced by the parser.
    /// </summary>
    [TestMethod]
    public void Serialize_ProducesCorrectUrlString()
    {
        var tree = new DefaultUrlParser().Parse("/test?param=value");
        const string expectedUrl = "/test?param=value";

        var result = this.serializer!.Serialize(tree);

        _ = result.Should().Be(expectedUrl);
    }
}
