// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

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
    public void SourceMountInAnOutputFolderIsRefusedAndOneOutsideTheRootIsNot()
    {
        var fileSystem = new MockFileSystem();
        _ = fileSystem.Directory.CreateDirectory(@"C:\Games\Demo\.cooked");
        _ = fileSystem.Directory.CreateDirectory(@"C:\Games\Media");

        var table = ProjectMounts.Resolve(
            fileSystem,
            FileSystemPath.From(@"C:\Games\Demo"),
            [new ProjectMount("Generated", MountKind.Source, ".cooked"), new ProjectMount("Media", MountKind.Source, "../Media")]);

        _ = table.Findings.Should().ContainSingle().Which.Should().Match<MountFinding>(
            static finding => finding.Code == MountCodes.FolderInOutput && finding.Mount == "Generated");
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
