// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Verifies persisted source priority without depending on discovery or cook timing.</summary>
[TestClass]
public sealed class CookedContentOrderingTests
{
    /// <summary>New libraries are below project output and above previously added libraries.</summary>
    [TestMethod]
    public void DefaultOrderPlacesNewLibrariesImmediatelyBelowProjectOutput()
    {
        LocalFolderMount[] folders = [new("Older", FileSystemPath.From(@"C:\Older")), new("Newer", FileSystemPath.From(@"C:\Newer"))];
        _ = CookedContentOrdering.Resolve(folders, [], []).Should().Equal(new CookedContentSource[]
        {
            new(CookedContentSourceKind.LocalFolder, "Older"), new(CookedContentSourceKind.LocalFolder, "Newer"), new(CookedContentSourceKind.ProjectOutput),
        });
    }

    /// <summary>Adding a library does not demote an explicit override above project output.</summary>
    [TestMethod]
    public void ExplicitPrioritySurvivesNewLibraryInsertionAndManifestRoundTrip()
    {
        var info = new ProjectInfo("Priority", Category.Games)
        {
            LocalFolderMounts = [new("Base", FileSystemPath.From(@"C:\Base")), new("Override", FileSystemPath.From(@"C:\Override")), new("New", FileSystemPath.From(@"C:\New"))],
            CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "Base"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Override")],
        };
        info.CookedContentOrder = CookedContentOrdering.Resolve(info).ToList();
        var json = ProjectInfo.ToJson(info);
        _ = json.Should().Contain("\"Kind\": \"ProjectOutput\"");
        var restored = ProjectInfo.FromJson(json);
        restored.Location = @"C:\Priority";
        _ = restored.CookedContentOrder.Should().Equal(info.CookedContentOrder);
        _ = ProjectContext.FromProjectInfo(restored, new MockFileSystem()).CookedContentOrder.Should().Equal(new CookedContentSource[]
        {
            new(CookedContentSourceKind.LocalFolder, "Base"), new(CookedContentSourceKind.LocalFolder, "New"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Override"),
        });
    }

    /// <summary>Pak files are libraries like folders: they join the order below project output and round-trip through the manifest.</summary>
    [TestMethod]
    public void PakFilesJoinTheOrderAndRoundTripThroughTheManifest()
    {
        var info = new ProjectInfo("Paks", Category.Games)
        {
            LocalFolderMounts = [new("Folder", FileSystemPath.From(@"C:\Folder"))],
            PakFileMounts = [new("Props", FileSystemPath.From(@"C:\Paks\Props.pak"))],
        };
        info.CookedContentOrder = CookedContentOrdering.Resolve(info).ToList();

        var restored = ProjectInfo.FromJson(ProjectInfo.ToJson(info));
        restored.Location = @"C:\Paks";

        _ = info.CookedContentOrder.Should().Equal(new CookedContentSource[]
        {
            new(CookedContentSourceKind.LocalFolder, "Folder"), new(CookedContentSourceKind.PakFile, "Props"), new(CookedContentSourceKind.ProjectOutput),
        });
        _ = restored.PakFileMounts.Should().Equal(info.PakFileMounts);
        _ = ProjectContext.FromProjectInfo(restored, new MockFileSystem()).PakFileMounts.Should().Equal(info.PakFileMounts);
        _ = CookedContentOrdering.Resolve(restored).Should().Equal(info.CookedContentOrder);
    }

    /// <summary>A mount name identifies one library, and the saved order must name each library with its own kind.</summary>
    [TestMethod]
    public void LibrariesAreIdentifiedByNameAndKind()
    {
        LocalFolderMount[] folders = [new("Props", FileSystemPath.From(@"C:\Props"))];
        PakFileMount[] paks = [new("Props", FileSystemPath.From(@"C:\Props.pak"))];
        PakFileMount[] pak = [new("Pak", FileSystemPath.From(@"C:\Pak.pak"))];

        Action shared = () => _ = CookedContentOrdering.Resolve(folders, paks, []);
        Action mislabeled = () => _ = CookedContentOrdering.Resolve([], pak, [new(CookedContentSourceKind.LocalFolder, "Pak")]);

        _ = shared.Should().Throw<InvalidDataException>();
        _ = mislabeled.Should().Throw<InvalidDataException>();
    }

    /// <summary>Malformed order entries are reported rather than changing precedence silently.</summary>
    /// <param name="kind">The malformed order case.</param>
    [TestMethod]
    [DataRow(0)]
    [DataRow(1)]
    [DataRow(2)]
    [DataRow(3)]
    public void InvalidPriorityIsRejected(int kind)
    {
        CookedContentSource[] order = kind switch
        {
            0 => [new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.ProjectOutput)],
            1 => [new(CookedContentSourceKind.LocalFolder, "Missing")],
            2 => [new((CookedContentSourceKind)99)],
            _ => [new(CookedContentSourceKind.ProjectOutput, "Invalid")],
        };
        Action resolve = () => _ = CookedContentOrdering.Resolve([], [], order);
        _ = resolve.Should().Throw<InvalidDataException>();
    }
}
