// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using Moq;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>Checks the Content mounts dialog: listing, adding, renaming, unmounting and the resulting project mounts.</summary>
[TestClass]
public sealed class ContentMountsViewModelTests
{
    public TestContext TestContext { get; set; } = null!;

    private static IEngineContentPak EnginePak { get; } = Mock.Of<IEngineContentPak>(pak => pak.Path == FileSystemPath.From("C:/SDK/share/oxygen/engine.pak"));

    /// <summary>Every mount is listed with its kind; the project's content cannot be renamed or unmounted.</summary>
    [TestMethod]
    public void ListsMountsWithKindsAndProtectsProjectContent()
    {
        var model = CreateModel(CreateProject());

        _ = model.Mounts.Select(static row => (row.Name, row.KindLabel)).Should().Equal(
            ("Content", "Project source"),
            ("Cooked", "Derived output"),
            ("Studio library", "Local library"),
            ("Engine", "Engine content"));
        var content = model.Mounts[0];
        _ = content.UnmountCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = content.RenameCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = content.Path.Should().Be("./Content");
        _ = model.Mounts[1].UnmountCommand.CanExecute(parameter: null).Should().BeTrue();
    }

    /// <summary>Known locations add once; names stay unique and free of separators.</summary>
    [TestMethod]
    public void AddingRefusesDuplicatesAndInvalidNames()
    {
        var model = CreateModel(CreateProject());

        model.SelectedKind = "Cooked";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.ErrorMessage.Should().Be("Cooked is already mounted.");

        model.SelectedKind = "Imported";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.HasError.Should().BeFalse();
        _ = model.Mounts[^2].Should().BeEquivalentTo(new { Name = "Imported", RelativePath = ".imported", IsNew = true, Kind = ContentMountKind.Derived });

        model.SelectedKind = ContentMountsViewModel.LocalFolderKind;
        model.NewMountName = "studio library";
        model.NewMountPath = this.TestContext.TestRunDirectory!;
        model.AddMountCommand.Execute(parameter: null);
        _ = model.ErrorMessage.Should().Be("A mount with this name already exists, or differs from one only in case.");
        model.NewMountName = "Art/Props";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.ErrorMessage.Should().Contain("slashes");
    }

    /// <summary>A project or derived mount names its virtual paths, so it follows the mount rule; a library is a label.</summary>
    [TestMethod]
    public void MountNamesFollowTheMountRuleAndLibrariesAreLabels()
    {
        var model = CreateModel(CreateProject());

        _ = model.ValidateName("Cooked Output", ContentMountKind.Derived).Should().Be(VirtualPathError.InvalidMountName.ToMessage());
        _ = model.ValidateName(".published", ContentMountKind.Derived).Should().Be(VirtualPathError.InvalidMountName.ToMessage());
        _ = model.ValidateName("Published", ContentMountKind.Derived).Should().BeNull();
        _ = model.ValidateName("Prop library", ContentMountKind.LocalLibrary).Should().BeNull();
    }

    /// <summary>A rename is checked when it is accepted, and an unfinished one blocks Apply until it is valid.</summary>
    [TestMethod]
    public void RenameValidatesBeforeApply()
    {
        var model = CreateModel(CreateProject());
        var cooked = model.Mounts[1];

        cooked.RenameCommand.Execute(parameter: null);
        _ = cooked.IsRenaming.Should().BeTrue();
        cooked.NameDraft = "Content";
        _ = model.Validate().Should().BeFalse();
        _ = model.ErrorMessage.Should().Be("A mount with this name already exists, or differs from one only in case.");

        cooked.NameDraft = "Published";
        _ = model.Validate().Should().BeTrue();
        _ = cooked.Name.Should().Be("Published");
        _ = cooked.IsRenaming.Should().BeFalse();
    }

    /// <summary>Applied rows rebuild the project mounts: renames, removals, and new derived and library mounts.</summary>
    [TestMethod]
    public void AppliedRowsRebuildProjectMounts()
    {
        var project = CreateProject();
        var model = CreateModel(project);
        model.Mounts[2].UnmountCommand.Execute(parameter: null);
        model.Mounts[1].RenameCommand.Execute(parameter: null);
        model.Mounts[1].NameDraft = "Published";
        _ = model.Validate().Should().BeTrue();
        model.SelectedKind = "Build";
        model.AddMountCommand.Execute(parameter: null);
        var library = Directory.CreateTempSubdirectory("cb-mount-");
        try
        {
            model.SelectedKind = ContentMountsViewModel.LocalFolderKind;
            model.NewMountName = "Library";
            model.NewMountPath = library.FullName;
            model.AddMountCommand.Execute(parameter: null);
            _ = model.HasError.Should().BeFalse(model.ErrorMessage);

            var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
            ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts, static _ => true);

            _ = candidate.AuthoringMounts.Select(static mount => (mount.Name, mount.RelativePath)).Should().Equal(
                ("Content", "Content"),
                ("Published", ".cooked"),
                ("Build", ".build"));
            _ = candidate.LocalFolderMounts.Select(static mount => mount.Name).Should().Equal("Library");
        }
        finally
        {
            library.Delete();
        }
    }

    /// <summary>The engine content is listed last and stays: it cannot be renamed or unmounted.</summary>
    [TestMethod]
    public void EngineContentIsAFixedLowestMount()
    {
        var engine = CreateModel(CreateProject()).Mounts[^1];

        _ = engine.Kind.Should().Be(ContentMountKind.EngineContent);
        _ = engine.Path.Should().Be(EnginePak.Path.FullPath);
        _ = engine.IsLocked.Should().BeTrue();
        _ = engine.HasActions.Should().BeFalse();
        _ = engine.RenameCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = engine.UnmountCommand.CanExecute(parameter: null).Should().BeFalse();
    }

    /// <summary>A pak file is a library: only an existing .pak file mounts, above the engine content, and Apply declares it, never the engine content.</summary>
    [TestMethod]
    public void PakFilesMountAsLibrariesAboveTheEngineContent()
    {
        var project = CreateProject();
        var model = CreateModel(project);
        var folder = Directory.CreateTempSubdirectory("cb-pak-");
        try
        {
            var pak = Path.Combine(folder.FullName, "Props.pak");
            var other = Path.Combine(folder.FullName, "Props.bin");
            File.WriteAllText(pak, "pak");
            File.WriteAllText(other, "not a pak");
            model.SelectedKind = ContentMountsViewModel.PakFileKind;
            model.NewMountName = "Props";

            model.NewMountPath = other;
            model.AddMountCommand.Execute(parameter: null);
            _ = model.HasError.Should().BeTrue();

            model.NewMountPath = pak;
            model.AddMountCommand.Execute(parameter: null);
            _ = model.HasError.Should().BeFalse(model.ErrorMessage);
            _ = model.Mounts.Select(static row => row.Kind).TakeLast(2).Should().Equal(ContentMountKind.PakLibrary, ContentMountKind.EngineContent);

            var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
            ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts, static _ => true);

            _ = candidate.PakFileMounts.Should().Equal(new PakFileMount("Props", FileSystemPath.From(pak)));
            _ = candidate.AuthoringMounts.Select(static mount => mount.Name).Concat(candidate.LocalFolderMounts.Select(static mount => mount.Name))
                .Should().NotContain(AssetPath.Engine.Mount);
        }
        finally
        {
            folder.Delete(recursive: true);
        }
    }

    /// <summary>A renamed pak keeps its place in the content priority.</summary>
    [TestMethod]
    public void RenamedPakKeepsItsPriority()
    {
        var project = CreateProject() with
        {
            PakFileMounts = [new("Props", FileSystemPath.From("D:/Paks/Props.pak"))],
            CookedContentOrder = [new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.PakFile, "Props")],
        };
        var model = CreateModel(project);
        var pak = model.Mounts.Single(static row => row.Kind == ContentMountKind.PakLibrary);
        pak.RenameCommand.Execute(parameter: null);
        pak.NameDraft = "Rocks";
        _ = model.Validate().Should().BeTrue();

        var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
        ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts, static _ => true);

        _ = candidate.CookedContentOrder.Should().Equal(new CookedContentSource(CookedContentSourceKind.ProjectOutput), new CookedContentSource(CookedContentSourceKind.PakFile, "Rocks"));
    }

    private static ContentMountsViewModel CreateModel(ProjectContext project) => new(project, Mock.Of<IDialogService>(), EnginePak);

    private static ProjectContext CreateProject()
        => TestProject.Create(new MockFileSystem(), "C:/Projects/Mounts").Context with
        {
            Name = "Mounts",
            AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")],
            LocalFolderMounts = [new("Studio library", FileSystemPath.From("D:/Studio/EnvironmentLibrary"))],
        };
}
