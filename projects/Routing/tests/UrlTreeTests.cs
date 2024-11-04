// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Tests;

using System.Diagnostics.CodeAnalysis;
using DroidNet.TestHelpers;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

/// <summary>
/// Contains unit tests for the <see cref="UrlTree" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class UrlTreeTests : TestSuiteWithAssertions
{
    /// <summary>
    /// Tests that a new instance of UrlTree has an empty segment group root
    /// when no root is provided.
    /// </summary>
    [TestMethod]
    public void GivenNullRoot_WhenNewUrlTreeCreated_ThenRootIsEmptySegmentGroup()
    {
        var sut = new UrlTree();

        _ = sut.Root.Segments.Should().BeEmpty();
    }

    /// <summary>
    /// Tests that a new instance of UrlTree has an empty dictionary for query
    /// parameters when no query params are provided.
    /// </summary>
    [TestMethod]
    public void GivenNullQueryParams_WhenNewUrlTreeCreated_ThenQueryParamsIsEmptyDictionary()
    {
        var sut = new UrlTree();

        _ = sut.QueryParams.Should().BeEmpty();
    }

    /// <summary>
    /// Tests that no exception is thrown when creating a new UrlTree instance
    /// with null root and query parameters.
    /// </summary>
    [TestMethod]
    public void GivenNullRootAndNullQueryParams_WhenNewUrlTreeCreated_ThenNoExceptionThrown()
    {
        Action action = () => _ = new UrlTree();

        _ = action.Should().NotThrow();
    }

    /// <summary>
    /// Tests that a debug assertion fails when creating a new UrlTree instance
    /// with a root containing segments.
    /// </summary>
    [TestMethod]
    public void GivenRootWithSegments_WhenNewUrlTreeCreatedWithNullQueryParams_ThenAssertionExceptionThrown()
    {
        var rootSegmentGroup = new UrlSegmentGroup(new[] { It.IsAny<UrlSegment>() }.ToList().AsReadOnly());

        _ = new UrlTree(rootSegmentGroup);

#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#endif
    }
}
