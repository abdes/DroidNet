// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.LivePreview.Tests.Session;

/// <summary>Checks which content-mount changes are accepted: the mount table decides, and opening never fails on a folder.</summary>
[TestClass]
[TestCategory("Mounts")]
public sealed class MountChangeTests
{
    private readonly MockFileSystem fileSystem = new();

    [TestMethod]
    public void AnOverlappingSourceMountIsRefused()
    {
        var expected = this.Project();
        var candidate = TestProject.InfoOf(expected);
        candidate.Mounts.Add(new ProjectMount("Nested", MountKind.Source, "Content/Nested"));

        var change = () => MountChange.CandidateContext(expected, candidate);

        _ = change.Should().Throw<InvalidDataException>().WithMessage($"*{MountCodes.FolderOverlap}*");
    }

    [TestMethod]
    [DataRow(".cooked", DisplayName = "The cooked output folder")]
    [DataRow("Content/.pipeline", DisplayName = "An output folder inside a mount")]
    public void AnOutputFolderCannotBeAddedAsASourceMount(string path)
    {
        var expected = this.Project();
        var candidate = TestProject.InfoOf(expected);
        candidate.Mounts.Add(new ProjectMount("Generated", MountKind.Source, path));

        var change = () => MountChange.CandidateContext(expected, candidate);

        _ = change.Should().Throw<InvalidDataException>().WithMessage($"*{MountCodes.FolderInOutput}*");
    }

    [TestMethod]
    public void AnAbsoluteCommittedPathIsRefused()
    {
        var expected = this.Project();
        var candidate = TestProject.InfoOf(expected);
        candidate.Mounts.Add(new ProjectMount("Lib", MountKind.Cooked, @"D:\Library"));

        var change = () => MountChange.CandidateContext(expected, candidate);

        _ = change.Should().Throw<InvalidDataException>().WithMessage($"*{MountCodes.AbsoluteCommitted}*");
    }

    [TestMethod]
    [DataRow("Cooked")]
    [DataRow("build")]
    [DataRow("Engine")]
    public void AReservedNameIsRefused(string name)
    {
        var expected = this.Project();
        var source = TestProject.InfoOf(expected);
        source.Mounts.Add(new ProjectMount(name, MountKind.Source, "Media"));
        var library = TestProject.InfoOf(expected);
        library.Mounts.Add(new ProjectMount(name, MountKind.Cooked, "Library"));

        var sourceChange = () => MountChange.CandidateContext(expected, source);
        var libraryChange = () => MountChange.CandidateContext(expected, library);

        _ = sourceChange.Should().Throw<InvalidDataException>().WithMessage($"*{MountCodes.NameInvalid}*");
        _ = libraryChange.Should().Throw<InvalidDataException>().WithMessage($"*{MountCodes.NameInvalid}*");
    }

    [TestMethod]
    public void AMissingLibraryFolderIsAcceptedAsAMissingMount()
    {
        var expected = this.Project();
        var candidate = TestProject.InfoOf(expected);
        candidate.Mounts.Add(new ProjectMount("Lib", MountKind.Cooked, "Gone"));

        var next = MountChange.CandidateContext(expected, candidate);

        _ = next.Mounts.Should().ContainSingle(mount => mount.Name == "Lib").Which.State.Should().Be(MountState.Missing);
    }

    [TestMethod]
    public void AnUnboundLibraryIsAcceptedAsAnUnboundMount()
    {
        var expected = this.Project();
        var candidate = TestProject.InfoOf(expected);
        candidate.Mounts.Add(new ProjectMount("Lib", MountKind.Cooked));

        var next = MountChange.CandidateContext(expected, candidate);

        _ = next.Mounts.Should().ContainSingle(mount => mount.Name == "Lib").Which.State.Should().Be(MountState.Unbound);
    }

    [TestMethod]
    public void AnOutputFolderViewIsShownWithoutBecomingAMount()
    {
        var expected = this.Project();
        var candidate = TestProject.InfoOf(expected);
        candidate.OutputFolderViews.Add(new OutputFolderView(ProjectOutputFolders.Cooked));

        var next = MountChange.CandidateContext(expected, candidate);

        _ = next.OutputFolderViews.Should().ContainSingle(view => view.Name == "Cooked");
        _ = next.SourceMounts.Should().OnlyContain(mount => !ProjectOutputFolders.Contains(mount.DeclaredPath!));
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

    private ProjectContext Project() => TestProject.Create(this.fileSystem, @"C:\Project", [new ProjectMount("Content", MountKind.Source, "Content")]).Context;
}
