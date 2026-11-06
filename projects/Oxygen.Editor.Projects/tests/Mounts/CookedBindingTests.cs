// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Persistence.Publication;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Projects.Tests.Mounts;

/// <summary>
/// A cooked mount bound to a library project's <c>.cooked</c> folder reads the generation that folder selects for the
/// mount's root. The engine never reads this format, so these cases are the editor's own.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Unit")]
public sealed class CookedBindingTests
{
    private const string ProjectRoot = @"C:\Studio\Game";
    private const string LibraryCooked = @"C:\Studio\Props\.cooked";

    [TestMethod]
    public void ACookedFolder_SelectsTheGenerationOfTheMountsRoot()
    {
        var generation = Guid.CreateVersion7();
        var fileSystem = FilesOf(("Props", generation));
        WriteGeneration(fileSystem, generation, "/Props/Crate.omat");

        var mount = Resolve(fileSystem);

        _ = mount.State.Should().Be(MountState.Bound);
        _ = mount.Container.Should().Be(MountContainer.Generation);
        _ = mount.BoundTo.Should().Be(FileSystemPath.From(LibraryCooked));
        _ = mount.Location.Should().Be(FileSystemPath.From(Path.Combine(LibraryCooked, "generations", generation.ToString("N"))));
        _ = mount.Role.Should().Be(MountRole.Shared);
        _ = mount.Library!.Holds("/Props/Crate.omat").Should().BeTrue();
    }

    [TestMethod]
    public void ACookedFolderWithoutTheRoot_IsInvalid()
    {
        var mount = Resolve(FilesOf(("Vehicles", Guid.CreateVersion7())));

        _ = mount.State.Should().Be(MountState.Invalid);
        _ = mount.Findings.Should().ContainSingle().Which.Code.Should().Be(MountCodes.CookedRootMismatch);
    }

    [TestMethod]
    public void ACookedFolderWhoseGenerationIsGone_IsInvalid()
    {
        var mount = Resolve(FilesOf(("Props", Guid.CreateVersion7())));

        _ = mount.State.Should().Be(MountState.Invalid);
        _ = mount.Findings.Should().ContainSingle().Which.Code.Should().Be(MountCodes.CookedFormatUnsupported);
    }

    [TestMethod]
    public void ACookedFolderWhoseDocumentIsNotTheHeads_IsInvalid()
    {
        var fileSystem = new MockFileSystem();
        foreach (var (path, bytes) in CookedLibraryFixture.Publication(("Props", Guid.CreateVersion7())))
        {
            var file = Path.Combine(LibraryCooked, path.Replace('/', '\\'));
            _ = fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(file)!);
            fileSystem.File.WriteAllBytes(file, path == CookPublicationFormat.HeadFileName ? bytes : [.. bytes, (byte)' ']);
        }

        var mount = Resolve(fileSystem);

        _ = mount.State.Should().Be(MountState.Invalid);
        _ = mount.Findings.Should().ContainSingle().Which.Code.Should().Be(MountCodes.CookedFormatUnsupported);
    }

    [TestMethod]
    public void AnInvalidMount_IsNotALibrary()
    {
        var fileSystem = FilesOf(("Vehicles", Guid.CreateVersion7()));
        var context = ContextOf(fileSystem);

        _ = context.LibraryMounts.Should().BeEmpty();
        _ = context.CookedMounts.Should().ContainSingle().Which.Name.Should().Be("Props");
        _ = MountCodes.IsWarning(context.MountFindings.Single().Code).Should().BeTrue();
    }

    private static MockFileSystem FilesOf(params (string Root, Guid SourceKey)[] roots)
    {
        var fileSystem = new MockFileSystem();
        foreach (var (path, bytes) in CookedLibraryFixture.Publication(roots))
        {
            var file = Path.Combine(LibraryCooked, path.Replace('/', '\\'));
            _ = fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(file)!);
            fileSystem.File.WriteAllBytes(file, bytes);
        }

        return fileSystem;
    }

    private static void WriteGeneration(MockFileSystem fileSystem, Guid generation, params string[] virtualPaths)
    {
        var folder = Path.Combine(LibraryCooked, "generations", generation.ToString("N"));
        _ = fileSystem.Directory.CreateDirectory(folder);
        fileSystem.File.WriteAllBytes(Path.Combine(folder, LooseCookedIndex.FileName), CookedLibraryFixture.LooseIndex(virtualPaths));
    }

    private static ResolvedMount Resolve(MockFileSystem fileSystem)
        => ContextOf(fileSystem).CookedMounts.Single();

    private static ProjectContext ContextOf(MockFileSystem fileSystem)
    {
        _ = fileSystem.Directory.CreateDirectory(ProjectRoot);
        fileSystem.File.WriteAllText(@"C:\Studio\.mounts.json", $$$"""{"schema_version":{{{ProjectMounts.SchemaVersion}}},"root":true}""");
        var info = new ProjectInfo("Game", Category.Games, ProjectRoot)
        {
            Mounts = [new ProjectMount("Content", MountKind.Source, "Content"), new ProjectMount("Props", MountKind.Cooked, "../Props/.cooked")],
        };
        _ = fileSystem.Directory.CreateDirectory(Path.Combine(ProjectRoot, "Content"));
        return ProjectContext.FromProjectInfo(info, fileSystem);
    }
}
