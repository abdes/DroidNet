// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

/// <summary>An import writes into the project's own mounts; a source in a mount stays there, and one from anywhere else is
/// copied in with the model.</summary>
[TestClass]
public sealed class ImportDestinationTests
{
    private const string RawMedia = @"D:\Media\Raw";
    private static readonly AssetPath Cars = AssetPath.Parse("/Content/Cars");

    /// <summary>V1: a source in a shared mount makes a model named in the chosen folder.</summary>
    [TestMethod]
    public void ASourceInAMountNamesTheModelInTheChosenFolder()
    {
        using var workspace = CreateWorkspace();

        var identity = ImportDestination.IdentityOf(workspace.Project, Path.Combine(RawMedia, "Cars", "Truck.fbx"), "Truck", Cars);

        _ = identity.Should().Be(AssetPath.Parse("/Content/Cars/Truck.fbx"));
    }

    /// <summary>V4: two settings of one source are two models of different names.</summary>
    [TestMethod]
    public void AnotherNameMakesAnotherModelOfTheSameSource()
    {
        using var workspace = CreateWorkspace();

        var identity = ImportDestination.IdentityOf(workspace.Project, Path.Combine(RawMedia, "Cars", "Truck.fbx"), "TruckLod", Cars);

        _ = identity.Should().Be(AssetPath.Parse("/Content/Cars/TruckLod.fbx"));
    }

    /// <summary>D1: a source in no mount is copied in, so the model is its file in a folder of its name.</summary>
    [TestMethod]
    public void ASourceInNoMountBecomesItsFileInAFolderOfItsName()
    {
        using var workspace = CreateWorkspace();

        var identity = ImportDestination.IdentityOf(workspace.Project, @"D:\Downloads\Lamp.glb", "Lamp", AssetPath.Parse("/Content/Props"));

        _ = identity.Should().Be(AssetPath.Parse("/Content/Props/Lamp/Lamp.glb"));
        _ = ImportDestination.IsMounted(workspace.Project, @"D:\Downloads\Lamp.glb").Should().BeFalse();
    }

    /// <summary>A source in an own mount imported beside itself is the model's companion, at the model's identity.</summary>
    [TestMethod]
    public void ASourceInAnOwnMountImportsBesideItselfByDefault()
    {
        using var workspace = CreateWorkspace();
        var source = workspace.Write("Content/Cars/Truck.fbx", "fbx");

        var folder = ImportDestination.OwnFolderOf(workspace.Project, source);

        _ = folder.Should().Be(Cars);
        _ = ImportDestination.IdentityOf(workspace.Project, source, "Truck", folder!).Should().Be(AssetPath.Parse("/Content/Cars/Truck.fbx"));
    }

    /// <summary>A source in a shared mount is not imported beside itself: settings there are made in its own project.</summary>
    [TestMethod]
    public void ASourceInASharedMountHasNoOwnFolder()
    {
        using var workspace = CreateWorkspace();

        _ = ImportDestination.OwnFolderOf(workspace.Project, Path.Combine(RawMedia, "Cars", "Truck.fbx")).Should().BeNull();
        _ = ImportDestination.OwnFolderOf(workspace.Project, @"D:\Downloads\Lamp.glb").Should().BeNull();
    }

    /// <summary>A destination in a shared mount is refused, whatever the source.</summary>
    /// <param name="source">The source's full path.</param>
    [TestMethod]
    [DataRow(@"D:\Media\Raw\Cars\Truck.fbx", DisplayName = "Source in a mount")]
    [DataRow(@"D:\Downloads\Lamp.glb", DisplayName = "Source in no mount")]
    public void ADestinationInASharedMountIsRefused(string source)
    {
        using var workspace = CreateWorkspace();

        var plan = () => ImportDestination.IdentityOf(workspace.Project, source, "Model", AssetPath.Parse("/RawMedia/Cars"));

        _ = plan.Should().Throw<ArgumentException>().WithMessage("*'RawMedia' is a shared mount*");
    }

    /// <summary>A selected folder in a shared mount starts the import in the first own mount; one in an own mount stays.</summary>
    [TestMethod]
    public void AnImportStartsInAnOwnMount()
    {
        using var workspace = CreateWorkspace();

        _ = ImportDestination.DefaultFolder(workspace.Project, AssetPath.Parse("/RawMedia/Cars")).Should().Be(AssetPath.Content);
        _ = ImportDestination.DefaultFolder(workspace.Project, Cars).Should().Be(Cars);
    }

    private static RelocationWorkspace CreateWorkspace()
    {
        var workspace = new RelocationWorkspace();
        workspace.MountOutside("RawMedia", RawMedia);
        workspace.WriteOutside(Path.Combine(RawMedia, "Cars", "Truck.fbx"), "fbx");
        return workspace;
    }
}
