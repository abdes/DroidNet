// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Managed.Core.Tests;

/// <summary>Checks how physical paths compare.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(FileSystemPath))]
public sealed class FileSystemPathTests
{
    private static readonly string Root = Path.Combine(Path.GetTempPath(), "Oxygen", "Project");

    [TestMethod]
    public void From_NormalizesTheSpelling()
    {
        var path = FileSystemPath.From(Root + Path.DirectorySeparatorChar);

        _ = path.FullPath.Should().Be(Path.GetFullPath(Root));
        _ = FileSystemPath.From(Path.Combine(Root, "Content", "..")).Should().Be(path);
        _ = FileSystemPath.From(Root.Replace(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)).Should().Be(path);
    }

    [TestMethod]
    public void Equality_FollowsTheFileSystemCase()
    {
        var path = FileSystemPath.From(Root);
        var other = FileSystemPath.From(Root.ToUpperInvariant());

        _ = (path == other).Should().Be(OperatingSystem.IsWindows());
        _ = (path.GetHashCode() == other.GetHashCode()).Should().Be(OperatingSystem.IsWindows());
    }

    [TestMethod]
    public void IsSameOrInside_ComparesWholeSegments()
    {
        var folder = FileSystemPath.From(Root);

        _ = folder.IsSameOrInside(folder).Should().BeTrue();
        _ = folder.Combine("Content/Red.omat.json").IsSameOrInside(folder).Should().BeTrue();
        _ = FileSystemPath.From(Root + "Old").IsSameOrInside(folder).Should().BeFalse();
        _ = folder.Combine("Content/Red.omat.json").GetPathBelow(folder).Should().Be("Content/Red.omat.json");
        _ = folder.GetPathBelow(folder).Should().BeEmpty();
    }

    [TestMethod]
    public void IsSameOrInside_RootFolder_HoldsEveryPath()
    {
        var root = FileSystemPath.From(Path.GetPathRoot(Root)!);

        _ = FileSystemPath.From(Root).IsSameOrInside(root).Should().BeTrue();
        _ = root.Parent.Should().BeNull();
    }
}
