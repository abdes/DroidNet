// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects.Mounts;
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
        ProjectMount[] folders = [new("Older", MountKind.Cooked), new("Newer", MountKind.Cooked)];
        _ = CookedContentOrdering.Resolve(folders, []).Should().Equal(
        [
            new(CookedContentSourceKind.Cooked, "Older"), new(CookedContentSourceKind.Cooked, "Newer"), new(CookedContentSourceKind.ProjectOutput),
        ]);
    }

    /// <summary>Adding a library does not demote an explicit override above project output.</summary>
    [TestMethod]
    public void ExplicitPrioritySurvivesNewLibraryInsertionAndManifestRoundTrip()
    {
        var info = new ProjectInfo("Priority", Category.Games)
        {
            Mounts = [new("Base", MountKind.Cooked), new("Override", MountKind.Cooked), new("New", MountKind.Cooked)],
            CookedContentOrder = [new(CookedContentSourceKind.Cooked, "Base"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.Cooked, "Override")],
        };
        info.CookedContentOrder = [.. CookedContentOrdering.Resolve(info)];
        var json = ProjectInfo.ToJson(info);
        _ = json.Should().Contain("\"Kind\": \"ProjectOutput\"");
        var restored = ProjectInfo.FromJson(json);
        restored.Location = @"C:\Priority";
        _ = restored.CookedContentOrder.Should().Equal(info.CookedContentOrder);
        _ = ProjectContext.FromProjectInfo(restored, new MockFileSystem()).CookedContentOrder.Should().Equal(
        [
            new(CookedContentSourceKind.Cooked, "Base"), new(CookedContentSourceKind.Cooked, "New"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.Cooked, "Override"),
        ]);
    }

    /// <summary>Pak files are libraries like folders: they join the order below project output and round-trip through the manifest.</summary>
    [TestMethod]
    public void PakFilesJoinTheOrderAndRoundTripThroughTheManifest()
    {
        var info = new ProjectInfo("Paks", Category.Games)
        {
            Mounts = [new("Folder", MountKind.Cooked), new("Props", MountKind.Pak, "Props.pak")],
        };
        info.CookedContentOrder = [.. CookedContentOrdering.Resolve(info)];

        var restored = ProjectInfo.FromJson(ProjectInfo.ToJson(info));
        restored.Location = @"C:\Paks";

        _ = info.CookedContentOrder.Should().Equal(
        [
            new(CookedContentSourceKind.Cooked, "Folder"), new(CookedContentSourceKind.Pak, "Props"), new(CookedContentSourceKind.ProjectOutput),
        ]);
        _ = restored.Mounts.Should().Equal(info.Mounts);
        _ = ProjectContext.FromProjectInfo(restored, new MockFileSystem()).DeclaredMounts.Should().Equal(info.Mounts);
        _ = CookedContentOrdering.Resolve(restored).Should().Equal(info.CookedContentOrder);
    }

    /// <summary>A mount name identifies one library, and the saved order must name each library with its own kind.</summary>
    [TestMethod]
    public void LibrariesAreIdentifiedByNameAndKind()
    {
        ProjectMount[] libraries = [new("Props", MountKind.Cooked), new("Props", MountKind.Pak)];
        ProjectMount[] pak = [new("Pak", MountKind.Pak)];

        Action shared = () => _ = CookedContentOrdering.Resolve(libraries, []);
        Action mislabeled = () => _ = CookedContentOrdering.Resolve(pak, [new(CookedContentSourceKind.Cooked, "Pak")]);

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
            1 => [new(CookedContentSourceKind.Cooked, "Missing")],
            2 => [new((CookedContentSourceKind)99)],
            _ => [new(CookedContentSourceKind.ProjectOutput, "Invalid")],
        };
        Action resolve = () => _ = CookedContentOrdering.Resolve([], order);
        _ = resolve.Should().Throw<InvalidDataException>();
    }
}
