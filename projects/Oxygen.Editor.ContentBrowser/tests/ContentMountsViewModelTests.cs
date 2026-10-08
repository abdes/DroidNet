// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using Moq;
using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks the Content mounts dialog: listing, adding, renaming, unmounting and the resulting project mounts.</summary>
[TestClass]
public sealed class ContentMountsViewModelTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Every mount is listed with its kind; the project's content cannot be renamed or unmounted.</summary>
    [TestMethod]
    public void ListsMountsWithKindsAndProtectsProjectContent()
    {
        var model = new ContentMountsViewModel(CreateProject(), Mock.Of<IDialogService>());

        _ = model.Mounts.Select(static row => (row.Name, row.KindLabel)).Should().Equal(
            ("Content", "Project source"),
            ("Cooked", "Derived output"),
            ("Studio library", "Local library"));
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
        var model = new ContentMountsViewModel(CreateProject(), Mock.Of<IDialogService>());

        model.SelectedKind = "Cooked";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.ErrorMessage.Should().Be("Cooked is already mounted.");

        model.SelectedKind = "Imported";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.HasError.Should().BeFalse();
        _ = model.Mounts[^1].Should().BeEquivalentTo(new { Name = "Imported", RelativePath = ".imported", IsNew = true, Kind = ContentMountKind.Derived });

        model.SelectedKind = ContentMountsViewModel.LocalFolderKind;
        model.NewMountName = "studio library";
        model.NewMountPath = this.TestContext.TestRunDirectory!;
        model.AddMountCommand.Execute(parameter: null);
        _ = model.ErrorMessage.Should().Be("A mount with this name already exists.");
        model.NewMountName = "Art/Props";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.ErrorMessage.Should().Contain("slashes");
    }

    /// <summary>A rename is checked when it is accepted, and an unfinished one blocks Apply until it is valid.</summary>
    [TestMethod]
    public void RenameValidatesBeforeApply()
    {
        var model = new ContentMountsViewModel(CreateProject(), Mock.Of<IDialogService>());
        var cooked = model.Mounts[1];

        cooked.RenameCommand.Execute(parameter: null);
        _ = cooked.IsRenaming.Should().BeTrue();
        cooked.NameDraft = "Content";
        _ = model.Validate().Should().BeFalse();
        _ = model.ErrorMessage.Should().Be("A mount with this name already exists.");

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
        var model = new ContentMountsViewModel(project, Mock.Of<IDialogService>());
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

            var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot);
            ProjectLayoutViewModel.ApplyMountRows(project, candidate, model.Mounts, static _ => true);

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

    private static ProjectContext CreateProject() => new()
    {
        ProjectId = Guid.NewGuid(),
        Name = "Mounts",
        Category = Category.Games,
        ProjectRoot = "C:/Projects/Mounts",
        AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")],
        LocalFolderMounts = [new("Studio library", "D:/Studio/EnvironmentLibrary")],
        Scenes = [],
    };
}
