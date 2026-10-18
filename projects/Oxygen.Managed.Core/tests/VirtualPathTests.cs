// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Managed.Core.Tests;

/// <summary>Checks the virtual path helpers built on the rules.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(VirtualPath))]
public sealed class VirtualPathTests
{
    [TestMethod]
    public void CreateAbsolute_MountAndRelativePath_JoinsThem()
    {
        var path = VirtualPath.CreateAbsolute("Content", "Materials/Wood.omat");

        _ = path.Should().Be("/Content/Materials/Wood.omat");
        _ = VirtualPath.IsCanonical(path).Should().BeTrue();
    }

    [TestMethod]
    public void CreateAbsolute_NoRelativePath_ReturnsMountRoot()
    {
        _ = VirtualPath.CreateAbsolute("Content").Should().Be("/Content");
        _ = VirtualPath.CreateAbsolute("Content", relativePath: string.Empty).Should().Be("/Content");
        _ = VirtualPath.CreateAbsolute(".cooked").Should().Be("/.cooked");
    }

    [TestMethod]
    public void CreateAbsolute_Backslashes_BecomeSeparators()
        => _ = VirtualPath.CreateAbsolute("Content", "Materials\\Wood.omat").Should().Be("/Content/Materials/Wood.omat");

    [TestMethod]
    [DataRow("", "a")]
    [DataRow("Con/tent", "a")]
    [DataRow("1Content", "a")]
    [DataRow("Content", "/Materials")]
    [DataRow("Content", "Materials//Wood.omat")]
    [DataRow("Content", "Materials/./Wood.omat")]
    [DataRow("Content", "Materials/../Wood.omat")]
    [DataRow("Content", "Materials/Wood:1.omat")]
    public void CreateAbsolute_RefusedMountOrSegment_Throws(string mount, string relativePath)
    {
        var act = () => VirtualPath.CreateAbsolute(mount, relativePath);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    [DataRow("Paint", "paint")]
    [DataRow("\u00C9cole", "\u00E9cole")]
    [DataRow("\u1E9E", "\u00DF")]
    public void FoldCase_CaseOnlySiblings_FoldEqual(string first, string second)
        => _ = VirtualPath.FoldCase(first).Should().Be(VirtualPath.FoldCase(second));

    [TestMethod]
    [DataRow(" Cafe\u0301 ", "Caf\u00E9")]
    [DataRow("Red", "Red")]
    [DataRow(" a\uD800", "a\uD800")]
    public void NormalizeTypedName_TypedText_IsTrimmedAndInNfc(string typed, string expected)
        => _ = VirtualPath.NormalizeTypedName(typed).Should().Be(expected);

    [TestMethod]
    public void ToNfc_UnpairedSurrogate_ReturnsNull()
    {
        _ = VirtualPath.ToNfc("a\uD800").Should().BeNull();
        _ = VirtualPath.FoldCase("a\uD800").Should().BeNull();
        _ = VirtualPath.ValidateSegment("a\uD800").Should().Be(VirtualPathError.InvalidUtf8);
    }

    [TestMethod]
    [DataRow(".cooked", true)]
    [DataRow("SourceMedia", true)]
    [DataRow("Sources", false)]
    [DataRow("sourcemedia", false)]
    public void IsDerivedContent_Segments_FollowTheRule(string segment, bool expected)
        => _ = VirtualPath.IsDerivedContent(segment).Should().Be(expected);

    [TestMethod]
    [DataRow(".cooked", VirtualPathRuleSet.Cooked, null)]
    [DataRow(".cooked", VirtualPathRuleSet.Authored, VirtualPathError.DerivedContent)]
    [DataRow("SourceMedia", VirtualPathRuleSet.Authored, VirtualPathError.DerivedContent)]
    [DataRow("Red:1.omat", VirtualPathRuleSet.Authored, VirtualPathError.ReservedCharacter)]
    [DataRow("Red.omat", VirtualPathRuleSet.Authored, null)]
    public void ValidateSegment_RuleSet_AddsDerivedContentWhenAuthored(string segment, VirtualPathRuleSet rules, VirtualPathError? expected)
        => _ = VirtualPath.ValidateSegment(segment, rules).Should().Be(expected);

    [TestMethod]
    [DataRow(".cooked", VirtualPathRuleSet.Cooked, null)]
    [DataRow(".cooked", VirtualPathRuleSet.Authored, VirtualPathError.InvalidMountName)]
    [DataRow("Content", VirtualPathRuleSet.Authored, null)]
    public void ValidateMountName_RuleSet_ReservesTheDotForTheEngine(string mount, VirtualPathRuleSet rules, VirtualPathError? expected)
        => _ = VirtualPath.ValidateMountName(mount, rules).Should().Be(expected);

    [TestMethod]
    public void CaseFolded_CaseOnlySiblings_AreEqualWithEqualHashes()
    {
        var comparer = VirtualPathComparer.CaseFolded;

        _ = comparer.Equals("/Content/Paint.omat", "/Content/paint.omat").Should().BeTrue();
        _ = comparer.GetHashCode("/Content/Paint.omat").Should().Be(comparer.GetHashCode("/Content/paint.omat"));
        _ = comparer.Equals("/Content/Paint.omat", "/Content/Paints.omat").Should().BeFalse();
        _ = comparer.Equals(null, null).Should().BeTrue();
        _ = comparer.Equals("a", null).Should().BeFalse();
    }

    [TestMethod]
    public void ToMessage_EveryError_HasAnExplanation()
    {
        foreach (var error in Enum.GetValues<VirtualPathError>())
        {
            _ = error.ToMessage().Should().NotBeNullOrWhiteSpace();
        }
    }
}
