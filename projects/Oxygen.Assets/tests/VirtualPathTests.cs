// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Assets.Filesystem;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class VirtualPathTests
{
    [TestMethod]
    public void IsCanonicalAbsolute_ShouldAcceptRoot()
    {
        _ = VirtualPath.IsCanonicalAbsolute("/").Should().BeTrue();
    }

    [TestMethod]
    public void IsCanonicalAbsolute_ShouldRejectMissingLeadingSlash()
    {
        _ = VirtualPath.IsCanonicalAbsolute("Content/Materials/Wood.omat").Should().BeFalse();
    }

    [TestMethod]
    public void IsCanonicalAbsolute_ShouldRejectDoubleSlash()
    {
        _ = VirtualPath.IsCanonicalAbsolute("/Content//Materials/Wood.omat").Should().BeFalse();
    }

    [TestMethod]
    public void IsCanonicalAbsolute_ShouldRejectDotSegments()
    {
        _ = VirtualPath.IsCanonicalAbsolute("/Content/./Materials").Should().BeFalse();
        _ = VirtualPath.IsCanonicalAbsolute("/Content/../Materials").Should().BeFalse();
    }

    [TestMethod]
    public void IsCanonicalAbsolute_ShouldRejectBackslashes()
    {
        _ = VirtualPath.IsCanonicalAbsolute("/Content\\Materials\\Wood.omat").Should().BeFalse();
    }

    [TestMethod]
    public void CreateAbsolute_ShouldBuildCanonicalPath()
    {
        var path = VirtualPath.CreateAbsolute("Content", "Materials/Wood.omat");

        _ = path.Should().Be("/Content/Materials/Wood.omat");
        _ = VirtualPath.IsCanonicalAbsolute(path).Should().BeTrue();
    }

    [TestMethod]
    public void CreateAbsolute_WhenRelativeIsNullOrEmpty_ShouldReturnMountRoot()
    {
        _ = VirtualPath.CreateAbsolute("Content").Should().Be("/Content");
        _ = VirtualPath.CreateAbsolute("Content", relativePath: string.Empty).Should().Be("/Content");
        _ = VirtualPath.CreateAbsolute("Content", relativePath: null).Should().Be("/Content");
    }

    [TestMethod]
    public void CreateAbsolute_ShouldNormalizeBackslashesInRelativePath()
    {
        _ = VirtualPath.CreateAbsolute("Content", "Materials\\Wood.omat").Should().Be("/Content/Materials/Wood.omat");
    }

    [TestMethod]
    public void CreateAbsolute_ShouldRejectInvalidInputs()
    {
        _ = new Action(() => VirtualPath.CreateAbsolute(string.Empty, "a")).Should().Throw<ArgumentException>();
        _ = new Action(() => VirtualPath.CreateAbsolute("Con/tent", "a")).Should().Throw<ArgumentException>();
        _ = new Action(() => VirtualPath.CreateAbsolute("Content", "/Materials")).Should().Throw<ArgumentException>();
        _ = new Action(() => VirtualPath.CreateAbsolute("Content", "Materials//Wood.omat")).Should().Throw<ArgumentException>();
        _ = new Action(() => VirtualPath.CreateAbsolute("Content", "Materials/./Wood.omat")).Should().Throw<ArgumentException>();
        _ = new Action(() => VirtualPath.CreateAbsolute("Content", "Materials/../Wood.omat")).Should().Throw<ArgumentException>();
    }
}
