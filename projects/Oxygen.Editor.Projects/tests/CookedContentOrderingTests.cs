// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Verifies persisted source priority without depending on discovery or cook timing.</summary>
[TestClass]
public sealed class CookedContentOrderingTests
{
    /// <summary>New libraries are below project output and above previously added libraries.</summary>
    [TestMethod]
    public void DefaultOrderPlacesNewLibrariesImmediatelyBelowProjectOutput()
    {
        LocalFolderMount[] folders = [new("Older", @"C:\Older"), new("Newer", @"C:\Newer")];
        _ = CookedContentOrdering.Resolve(folders, []).Should().Equal(new CookedContentSource[]
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
            LocalFolderMounts = [new("Base", @"C:\Base"), new("Override", @"C:\Override"), new("New", @"C:\New")],
            CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "Base"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Override")],
        };
        info.CookedContentOrder = CookedContentOrdering.Resolve(info.LocalFolderMounts, info.CookedContentOrder).ToList();
        var json = ProjectInfo.ToJson(info);
        _ = json.Should().Contain("\"Kind\": \"ProjectOutput\"");
        var restored = ProjectInfo.FromJson(json);
        _ = restored.CookedContentOrder.Should().Equal(info.CookedContentOrder);
        _ = ProjectContext.FromProjectInfo(restored).CookedContentOrder.Should().Equal(new CookedContentSource[]
        {
            new(CookedContentSourceKind.LocalFolder, "Base"), new(CookedContentSourceKind.LocalFolder, "New"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Override"),
        });
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
        Action resolve = () => _ = CookedContentOrdering.Resolve([], order);
        _ = resolve.Should().Throw<InvalidDataException>();
    }
}
