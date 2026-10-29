// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.LivePreview.Tests.Session;

/// <summary>Checks which content-mount changes are accepted: output folders are never mounts, reserved names are refused.</summary>
[TestClass]
[TestCategory("Mounts")]
public sealed class MountChangeTests
{
    private readonly MockFileSystem fileSystem = new();

    [TestMethod]
    [DataRow(".cooked", DisplayName = "The cooked output folder")]
    [DataRow("Content/.pipeline", DisplayName = "An output folder inside a mount")]
    public void AnOutputFolderCannotBeAddedAsAnAuthoringMount(string path)
    {
        var expected = this.Project();
        var candidate = TestProject.InfoOf(expected);
        candidate.AuthoringMounts.Add(new ProjectMountPoint("Generated", path));

        var change = () => MountChange.CandidateContext(expected, candidate);

        _ = change.Should().Throw<InvalidDataException>().WithMessage("*output folders are never mounts*");
    }

    [TestMethod]
    [DataRow("Cooked")]
    [DataRow("build")]
    [DataRow("Engine")]
    public void AReservedNameIsRefused(string name)
    {
        var expected = this.Project();
        var authoring = TestProject.InfoOf(expected);
        authoring.AuthoringMounts.Add(new ProjectMountPoint(name, "Media"));
        var library = TestProject.InfoOf(expected);
        library.LocalFolderMounts.Add(new LocalFolderMount(name, FileSystemPath.From(@"D:\Library")));
        _ = this.fileSystem.Directory.CreateDirectory(@"D:\Library");

        var authoringChange = () => MountChange.CandidateContext(expected, authoring);
        var libraryChange = () => MountChange.CandidateContext(expected, library);

        _ = authoringChange.Should().Throw<InvalidDataException>();
        _ = libraryChange.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void AnOutputFolderViewIsShownWithoutBecomingAMount()
    {
        var expected = this.Project();
        var candidate = TestProject.InfoOf(expected);
        candidate.OutputFolderViews.Add(new OutputFolderView(ProjectOutputFolders.Cooked));

        var next = MountChange.CandidateContext(expected, candidate);

        _ = next.OutputFolderViews.Should().ContainSingle(view => view.Name == "Cooked");
        _ = next.AuthoringMounts.Should().OnlyContain(mount => !ProjectOutputFolders.Contains(mount));
    }

    [TestMethod]
    public void OnlyCookedAndBuildCanBeShown()
    {
        var expected = this.Project();
        var candidate = TestProject.InfoOf(expected);
        candidate.OutputFolderViews.Add(new OutputFolderView(ProjectOutputFolders.Pipeline));

        var change = () => MountChange.CandidateContext(expected, candidate);

        _ = change.Should().Throw<InvalidDataException>();
    }

    private ProjectContext Project() => TestProject.Create(this.fileSystem, @"C:\Project", [new ProjectMountPoint("Content", "Content")]).Context;
}
