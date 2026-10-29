// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Checks which project paths lie in the pipeline's output folders.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class ProjectOutputFoldersTests
{
    [TestMethod]
    [DataRow(".cooked/x", true)]
    [DataRow(".cooked/generations", true)]
    [DataRow(".scratch/Content/Ship.ogeo", true)]
    [DataRow(@".build\cache", true)]
    [DataRow("Content/.pipeline/state.json", true)]
    [DataRow("Content/Materials/Red.omat.json", false)]
    [DataRow(".", false)]
    [DataRow("./Content", false)]
    [DataRow("SourceMedia/DCC", false)]
    [DataRow("../x", false)]
    [DataRow("../../Media", false)]
    [DataRow("../.cooked/x", false)]
    [DataRow("Content/../.cooked/x", true)]
    [DataRow("Content/../../Shared/.cooked", false)]
    [DataRow("C:/Studio/.cooked", false)]
    public void PathInADotFolderIsOutput(string path, bool expected)
        => _ = ProjectOutputFolders.Contains(path).Should().Be(expected);

    [TestMethod]
    public void MountInAnOutputFolderIsOutput()
    {
        _ = ProjectOutputFolders.Contains(new ProjectMountPoint("Generated", ".cooked")).Should().BeTrue();
        _ = ProjectOutputFolders.Contains(new ProjectMountPoint("Media", "../Media")).Should().BeFalse();
    }

    [TestMethod]
    public void PathsAreBuiltUnderEachOutputFolder()
    {
        var root = Path.Combine("C:", "Project");

        _ = ProjectOutputFolders.CookedPath(root, "head.json").Should().Be(Path.Combine(root, ".cooked", "head.json"));
        _ = ProjectOutputFolders.BuildPath(root, "play", "launch.json").Should().Be(Path.Combine(root, ".build", "play", "launch.json"));
        _ = ProjectOutputFolders.PipelinePath(root, "Scenes").Should().Be(Path.Combine(root, ".pipeline", "Scenes"));
    }

    [TestMethod]
    public void OnlyCookedAndBuildHaveExplorerNodes()
    {
        _ = ProjectOutputFolders.ViewName(ProjectOutputFolders.Cooked).Should().Be("Cooked");
        _ = ProjectOutputFolders.ViewName(ProjectOutputFolders.Build).Should().Be("Build");
        _ = new OutputFolderView(ProjectOutputFolders.Build).Name.Should().Be("Build");

        var pipeline = () => ProjectOutputFolders.ViewName(ProjectOutputFolders.Pipeline);

        _ = pipeline.Should().Throw<ArgumentException>();
    }
}
