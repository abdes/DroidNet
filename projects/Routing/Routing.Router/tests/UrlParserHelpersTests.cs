// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Routing.Detail;
using DroidNet.TestHelpers;

namespace DroidNet.Routing.Tests;

/// <summary>Unit tests for <see cref="UrlParserHelpers" />.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("URL Parsing")]
public class UrlParserHelpersTests : TestSuiteWithAssertions
{
    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.PeekStartsWith(ref ReadOnlySpan{char},char)" />
    /// when the span starts with the given character.
    /// </summary>
    [TestMethod]
    public void PeekStartsWith_Char_Present_ReturnsTrueAndDoesNotModifySpan()
    {
        const string test = "test";
        var remaining = test.AsSpan();

        var result = remaining.PeekStartsWith('t');

        _ = result.Should().BeTrue();
        _ = remaining.ToString().Should().Be(test);
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.PeekStartsWith(ref ReadOnlySpan{char},char)" />
    /// when the span does not start with the given character.
    /// </summary>
    [TestMethod]
    public void PeekStartsWith_Char_NotPresent_ReturnsFalseAndDoesNotModifySpan()
    {
        const string test = "test";
        var remaining = test.AsSpan();

        var result = remaining.PeekStartsWith('_');

        _ = result.Should().BeFalse();
        _ = remaining.ToString().Should().Be(test);
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.PeekStartsWith(ref ReadOnlySpan{char},string)" />
    /// when the span starts with the given string.
    /// </summary>
    [TestMethod]
    public void PeekStartsWith_String_Present_ReturnsTrueAndDoesNotModifySpan()
    {
        const string test = "test";
        var remaining = test.AsSpan();

        var result = remaining.PeekStartsWith("te");

        _ = result.Should().BeTrue();
        _ = remaining.ToString().Should().Be(test);
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.PeekStartsWith(ref ReadOnlySpan{char},string)" />
    /// when the span does not start with the given string.
    /// </summary>
    [TestMethod]
    public void PeekStartsWith_String_NotPresent_ReturnsFalseAndDoesNotModifySpan()
    {
        const string test = "test";
        var remaining = test.AsSpan();

        var result = remaining.PeekStartsWith("tex");

        _ = result.Should().BeFalse();
        _ = remaining.ToString().Should().Be(test);
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.ConsumeOptional(ref ReadOnlySpan{char},char)" />
    /// when the character is present at the start of the span at least once.
    /// </summary>
    [TestMethod]
    public void ConsumeOptional_Char_Present_RemovesAllConsecutiveMatches()
    {
        var remaining = "//test/".AsSpan();

        var result = remaining.ConsumeOptional('/');

        _ = result.Should().BeTrue();
        _ = remaining.ToString().Should().Be("/test/");
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.ConsumeOptional(ref ReadOnlySpan{char},char)" />
    /// when the character is not present at the start of the span.
    /// </summary>
    [TestMethod]
    public void ConsumeOptional_Char_NotPresent_ReturnsFalseAndDoesNotModifySpan()
    {
        var remaining = "test".AsSpan();
        var initialLength = remaining.Length;

        var result = remaining.ConsumeOptional('_');

        _ = result.Should().BeFalse();
        _ = remaining.Length.Should().Be(initialLength);
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.ConsumeOptional(ref ReadOnlySpan{char},string)" />
    /// when the string is present at the start of the span at least once.
    /// </summary>
    [TestMethod]
    public void ConsumeOptional_String_Present_RemovesAllConsecutiveMatches()
    {
        var remaining = "----test".AsSpan();
        var result = remaining.ConsumeOptional("--");
        _ = result.Should().BeTrue();
        _ = remaining.ToString().Should().Be("--test");
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.ConsumeOptional(ref ReadOnlySpan{char},string)" />
    /// when the string is present at the start of the span at least once.
    /// </summary>
    [TestMethod]
    public void ConsumeOptional_String_NotPresent_ReturnsFalseAndDoesNotModifySpan()
    {
        var remaining = "----test".AsSpan();
        var initialLength = remaining.Length;

        var result = remaining.ConsumeOptional("//");
        _ = result.Should().BeFalse();
        _ = remaining.Length.Should().Be(initialLength);
    }

    /// <summary>
    /// Tests <see cref="UrlParserHelpers.Capture(ref ReadOnlySpan{char},char)" />
    /// when the character is present at the start of the span at least once.
    /// </summary>
    [TestMethod]
    public void Capture_Char_Found_RemovesFromSpan()
    {
        var remaining = "test".AsSpan();
        remaining.Capture('t');
        _ = remaining.ToString().Should().Be("est");
    }

    /// <summary>
    /// Tests <see cref="UrlParserHelpers.Capture(ref ReadOnlySpan{char},char)" />
    /// when the character is not present at the start of the span.
    /// </summary>
    [TestMethod]
    public void Capture_Char_NotFound_DebugAssert_DoesNotModifySpan()
    {
        var remaining = "test".AsSpan();
        remaining.Capture('_');

#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#endif
        _ = remaining.ToString().Should().Be("test");
    }

    /// <summary>
    /// Tests <see cref="UrlParserHelpers.Capture(ref ReadOnlySpan{char},string)" />
    /// when the string is present at the start of the span at least once.
    /// </summary>
    [TestMethod]
    public void Capture_String_Found_RemovesFromSpan()
    {
        var remaining = "----test".AsSpan();
        remaining.Capture("--");
        _ = remaining.ToString().Should().Be("--test");
    }

    /// <summary>
    /// Tests <see cref="UrlParserHelpers.Capture(ref ReadOnlySpan{char},string)" />
    /// when the string is empty.
    /// </summary>
    [TestMethod]
    public void Capture_String_Empty_DoesNotModifySpan()
    {
        var remaining = "  test".AsSpan();
        remaining.Capture(string.Empty);
        _ = remaining.ToString().Should().Be("  test");
    }

    /// <summary>
    /// Tests <see cref="UrlParserHelpers.Capture(ref ReadOnlySpan{char},string)" />
    /// when the string is not present at the start of the span.
    /// </summary>
    [TestMethod]
    public void Capture_String_NotFound_DebugAssert_DoesNotModifySpan()
    {
        var remaining = "test".AsSpan();
        remaining.Capture("__");

#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#endif
        _ = remaining.ToString().Should().Be("test");
    }

    /// <summary>
    /// Tests <see cref="UrlParserHelpers.MatchSegment(ref ReadOnlySpan{char})" />.
    /// </summary>
    /// <param name="input">Input string.</param>
    [TestMethod]
    [DataRow("segment/next")]
    [DataRow("segment(group")]
    [DataRow("segment)end")]
    [DataRow("segment?query")]
    [DataRow("segment#frag")]
    public void MatchSegment_StopsAtSpecialCharacter(string input)
    {
        var remaining = input.AsSpan();
        var result = remaining.MatchSegment();
        _ = result.Should().Be("segment");
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.MatchQueryParamKey(ref ReadOnlySpan{char})" />.
    /// </summary>
    /// <param name="input">Input string.</param>
    [TestMethod]
    [DataRow("param=value")]
    [DataRow("param?query")]
    [DataRow("param&param")]
    [DataRow("param#frag")]
    public void MatchQueryParamKey_StopsAtSpecialCharacter(string input)
    {
        var remaining = input.AsSpan();
        var result = remaining.MatchQueryParamKey();
        _ = result.Should().Be("param");
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.MatchMatrixParamKey(ref ReadOnlySpan{char})" />.
    /// </summary>
    /// <param name="input">Input string.</param>
    [TestMethod]
    [DataRow("param/segment")]
    [DataRow("param(group")]
    [DataRow("param)end")]
    [DataRow("param?query")]
    [DataRow("param;more")]
    [DataRow("param=value")]
    [DataRow("param#frag")]
    public void MatchMatrixParamKey_StopsAtSpecialCharacter(string input)
    {
        var remaining = input.AsSpan();
        var result = remaining.MatchMatrixParamKey();
        _ = result.Should().Be("param");
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.MatchMatrixParamValue(ref ReadOnlySpan{char})" />.
    /// </summary>
    /// <param name="input">Input string.</param>
    [TestMethod]
    [DataRow("value/next")]
    [DataRow("value(group")]
    [DataRow("value)end")]
    [DataRow("value?query")]
    [DataRow("value#frag")]
    public void MatchMatrixParamValue_StopsAtSpecialCharacter(string input)
    {
        var remaining = input.AsSpan();
        var result = remaining.MatchMatrixParamValue();
        _ = result.Should().Be("value");
    }

    /// <summary>
    /// Tests
    /// <see cref="UrlParserHelpers.MatchQueryParamValue(ref ReadOnlySpan{char})" />.
    /// </summary>
    /// <param name="input">Input string.</param>
    [TestMethod]
    [DataRow("value&more")]
    [DataRow("value#frag")]
    public void MatchQueryParamValue_StopsAtSpecialCharacter(string input)
    {
        var remaining = input.AsSpan();
        var result = remaining.MatchQueryParamValue();
        _ = result.Should().Be("value");
    }
}
