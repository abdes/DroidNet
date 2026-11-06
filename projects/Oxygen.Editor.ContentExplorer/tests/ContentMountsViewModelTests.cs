// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using Moq;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>
/// Checks the Content mounts dialog: listing with each mount's binding on this machine, adding to Project.oxy or to this
/// machine only, binding, renaming, unmounting, and the resulting project mounts and .mounts.json changes.
/// </summary>
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
            ("Content", "Source"),
            ("Cooked", "Output"),
            ("StudioLibrary", "Cooked"),
            ("Engine", "Pak"));
        var content = model.Mounts[0];
        _ = content.UnmountCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = content.RenameCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = content.CommittedText.Should().Be("Content");
        _ = model.Mounts[1].UnmountCommand.CanExecute(parameter: null).Should().BeTrue();
    }

    /// <summary>Known locations add once; names stay unique and free of separators.</summary>
    [TestMethod]
    public void AddingRefusesDuplicatesAndInvalidNames()
    {
        var model = CreateModel(CreateProject());

        model.SelectedKind = "Cooked";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.ErrorMessage.Should().Be("Cooked is already shown.");

        model.SelectedKind = "Build";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.HasError.Should().BeFalse();
        _ = model.Mounts[^2].Should().BeEquivalentTo(new { Name = "Build", RelativePath = ProjectOutputFolders.Build, IsNew = true, Kind = ContentMountKind.Derived });

        model.SelectedKind = ContentMountsViewModel.LocalFolderKind;
        model.NewMountName = "studiolibrary";
        model.NewMountPath = "C:/Projects/Mounts/Content";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.ErrorMessage.Should().Be("A mount with this name already exists, or differs from one only in case.");
        model.NewMountName = "Art/Props";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.ErrorMessage.Should().Contain("starts with a letter");
    }

    /// <summary>Every mount kind follows the one rule of a declared mount name.</summary>
    [TestMethod]
    public void MountNamesFollowTheDeclaredNameRule()
    {
        var model = CreateModel(CreateProject());

        _ = model.ValidateName("Cooked Output").Should().Be(ProjectAssetFiles.ValidateDeclaredMountName("Cooked Output")).And.Contain("starts with a letter");
        _ = model.ValidateName(".published").Should().Contain("starts with a letter");
        _ = model.ValidateName("Prop library").Should().Contain("starts with a letter");
        _ = model.ValidateName("Published").Should().BeNull();
        _ = model.ValidateName("PropLibrary").Should().BeNull();
        _ = model.ValidateName("project").Should().NotBeNull("the project's own files have that mount");
        _ = model.ValidateName("Project").Should().NotBeNull("a library's name is its mount's");
    }

    /// <summary>A rename is checked when it is accepted, and an unfinished one blocks Apply until it is valid.</summary>
    [TestMethod]
    public void RenameValidatesBeforeApply()
    {
        var model = CreateModel(CreateProject());
        var library = model.Mounts[2];

        library.RenameCommand.Execute(parameter: null);
        _ = library.IsRenaming.Should().BeTrue();
        library.NameDraft = "Content";
        _ = model.Validate().Should().BeFalse();
        _ = model.ErrorMessage.Should().Be("A mount with this name already exists, or differs from one only in case.");

        library.NameDraft = "Published";
        _ = model.Validate().Should().BeTrue();
        _ = library.Name.Should().Be("Published");
        _ = library.IsRenaming.Should().BeFalse();
    }

    /// <summary>A derived output node has a fixed name: it cannot be renamed.</summary>
    [TestMethod]
    public void DerivedOutputRowsCannotBeRenamed()
    {
        var model = CreateModel(CreateProject());

        _ = model.Mounts[1].Kind.Should().Be(ContentMountKind.Derived);
        _ = model.Mounts[1].CanRename.Should().BeFalse();
        _ = model.Mounts[1].RenameCommand.CanExecute(parameter: null).Should().BeFalse();
    }

    /// <summary>Applied rows rebuild the project mounts: renames, removals, and new derived and library mounts.</summary>
    [TestMethod]
    public void AppliedRowsRebuildProjectMounts()
    {
        var project = CreateProject();
        _ = project.FileSystem.Directory.CreateDirectory("C:/Libraries/Library");
        var model = CreateModel(project);
        model.Mounts[2].UnmountCommand.Execute(parameter: null);
        model.SelectedKind = "Build";
        model.AddMountCommand.Execute(parameter: null);
        model.SelectedKind = ContentMountsViewModel.LocalFolderKind;
        model.NewMountName = "Library";
        model.NewMountPath = "C:/Libraries/Library";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.HasError.Should().BeFalse(model.ErrorMessage);

        var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
        ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts, static _ => true);

        _ = candidate.Mounts.Should().Equal(new ProjectMount("Content", MountKind.Source, "Content"), new ProjectMount("Library", MountKind.Cooked, "../../Libraries/Library"));
        _ = candidate.OutputFolderViews.Select(static view => view.Folder).Should().Equal(ProjectOutputFolders.Cooked, ProjectOutputFolders.Build);
        _ = model.GetBindingChanges().Changes.Should().BeEquivalentTo(
            new Dictionary<string, string?>(StringComparer.Ordinal) { ["StudioLibrary"] = null },
            "the unmounted library's binding leaves the project's .mounts.json");

        var folders = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
        ContentFoldersViewModel.ApplyMountRows(project, folders, model.Mounts, static _ => false);
        _ = folders.Mounts[^1].Should().Be(new ProjectMount("Library", MountKind.Source, "../../Libraries/Library"));
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
        _ = project.FileSystem.Directory.CreateDirectory("C:/Paks");
        project.FileSystem.File.WriteAllText("C:/Paks/Props.pak", "pak");
        project.FileSystem.File.WriteAllText("C:/Paks/Props.bin", "not a pak");
        var model = CreateModel(project);
        model.SelectedKind = ContentMountsViewModel.PakFileKind;
        model.NewMountName = "Props";

        model.NewMountPath = "C:/Paks/Props.bin";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.HasError.Should().BeTrue();

        model.NewMountPath = "C:/Paks/Props.pak";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.HasError.Should().BeFalse(model.ErrorMessage);
        _ = model.Mounts.Select(static row => row.Kind).TakeLast(2).Should().Equal(ContentMountKind.PakLibrary, ContentMountKind.EngineContent);

        var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
        ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts, static _ => true);

        _ = candidate.Mounts.Single(static mount => mount.Name == "Props").Should().Be(new ProjectMount("Props", MountKind.Cooked, "../../Paks/Props.pak"));
        _ = candidate.Mounts.Select(static mount => mount.Name).Should().NotContain(AssetPath.Engine.Mount);
    }

    /// <summary>A renamed pak keeps its place in the content priority.</summary>
    [TestMethod]
    public void RenamedPakKeepsItsPriority()
    {
        var project = CreateProject(new ProjectMount("Props", MountKind.Cooked, "../../Paks/Props.pak")) with
        {
            CookedContentOrder = [new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.Cooked, "Props")],
        };
        var model = CreateModel(project);
        var pak = model.Mounts.Single(static row => row.Kind == ContentMountKind.PakLibrary);
        pak.RenameCommand.Execute(parameter: null);
        pak.NameDraft = "Rocks";
        _ = model.Validate().Should().BeTrue();

        var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
        ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts, static _ => true);

        _ = candidate.CookedContentOrder.Should().Equal(new CookedContentSource(CookedContentSourceKind.ProjectOutput), new CookedContentSource(CookedContentSourceKind.Cooked, "Rocks"));
    }

    /// <summary>Each row shows where its mount is on this machine, and offers what fits: Bind, Override, Remove binding.</summary>
    [TestMethod]
    public void RowsShowEachMountsBindingOnThisMachine()
    {
        var project = CreateProject(new ProjectMount("Shared"), new ProjectMount("Media", MountKind.Source, "../Media"));
        project.FileSystem.Directory.Delete("C:/Projects/Media");
        var model = CreateModel(TestProject.Open(TestProject.InfoOf(project), project.FileSystem).Context);

        var rows = model.Mounts.ToDictionary(static row => row.Name, StringComparer.Ordinal);
        _ = rows["Content"].Binding.Should().Be(MountBindingState.Committed);
        _ = rows["Shared"].Binding.Should().Be(MountBindingState.NotBound);
        _ = rows["Shared"].BindActionText.Should().Be("Bind…");
        _ = rows["Shared"].IsPlaceholder.Should().BeTrue();
        _ = rows["Media"].Binding.Should().Be(MountBindingState.FolderMissing);
        _ = rows["Media"].BindActionText.Should().Be("Override…");
        _ = rows["StudioLibrary"].LocalLocation.Should().Be(Path.GetFullPath("D:/Studio/EnvironmentLibrary"));
        _ = rows["StudioLibrary"].IsRemoveBindingVisible.Should().BeTrue();
        _ = rows["Engine"].Binding.Should().Be(MountBindingState.Fixed);
    }

    /// <summary>Binding, rebinding, renaming and unmounting bound mounts change the project's own .mounts.json only.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task BindingChangesWriteTheProjectsOwnMountsFile()
    {
        var project = CreateProject(new ProjectMount("Shared"));
        _ = project.FileSystem.Directory.CreateDirectory("D:/Studio/Shared");
        _ = project.FileSystem.Directory.CreateDirectory("D:/Studio/Other");
        var dialogs = new Mock<IDialogService>();
        _ = dialogs.Setup(value => value.PickFolderAsync(It.IsAny<FolderPickerSpec>(), It.IsAny<CancellationToken>())).ReturnsAsync("D:/Studio/Shared");
        var model = new ContentMountsViewModel(project, dialogs.Object, EnginePak);
        var shared = model.Mounts.Single(static row => row.Name == "Shared");
        var library = model.Mounts.Single(static row => row.Name == "StudioLibrary");
        _ = model.GetBindingChanges().IsEmpty.Should().BeTrue();

        await shared.BindCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        library.RenameCommand.Execute(parameter: null);
        library.NameDraft = "Environment";
        _ = model.Validate().Should().BeTrue();

        _ = shared.Binding.Should().Be(MountBindingState.ThisMachine);
        _ = model.GetBindingChanges().Changes.Should().BeEquivalentTo(new Dictionary<string, string?>(StringComparer.Ordinal)
        {
            ["Shared"] = "D:/Studio/Shared",
            ["StudioLibrary"] = null,
            ["Environment"] = "D:/Studio/EnvironmentLibrary",
        });

        shared.RemoveBindingCommand.Execute(parameter: null);
        _ = shared.Binding.Should().Be(MountBindingState.NotBound);
        _ = model.GetBindingChanges().Changes.Keys.Should().NotContain("Shared");
    }

    /// <summary>
    /// A new folder is saved in Project.oxy when it is on the project's drive, and only on this machine otherwise: then
    /// Project.oxy commits a placeholder and the project's own .mounts.json binds it.
    /// </summary>
    [TestMethod]
    public void ANewFolderIsSavedInTheProjectOrOnThisMachine()
    {
        var project = CreateProject();
        _ = project.FileSystem.Directory.CreateDirectory("C:/Shared/Trees");
        _ = project.FileSystem.Directory.CreateDirectory("E:/Shared/Rocks");
        var model = CreateModel(project);
        model.SelectedKind = ContentMountsViewModel.LocalFolderKind;

        model.NewMountPath = "C:/Shared/Trees";
        _ = model.Placement.SaveInProject.Should().BeTrue("the folder is on the project's drive");
        model.NewMountName = "Trees";
        model.AddMountCommand.Execute(parameter: null);
        model.NewMountPath = "E:/Shared/Rocks";
        _ = model.Placement.CanSaveInProject.Should().BeFalse("no relative path reaches another drive");
        _ = model.Placement.SaveOnThisMachine.Should().BeTrue();
        model.NewMountName = "Rocks";
        model.AddMountCommand.Execute(parameter: null);

        var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
        ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts, static _ => false);
        _ = candidate.Mounts.Should().Contain(new ProjectMount("Trees", MountKind.Source, "../../Shared/Trees"));
        _ = candidate.Mounts.Should().Contain(new ProjectMount("Rocks", MountKind.Source, Path: null));
        _ = model.GetBindingChanges().Changes.Should().BeEquivalentTo(new Dictionary<string, string?>(StringComparer.Ordinal) { ["Rocks"] = "E:/Shared/Rocks" });
    }

    private static ContentMountsViewModel CreateModel(ProjectContext project) => new(project, Mock.Of<IDialogService>(), EnginePak);

    // The studio library is a placeholder that a mounts file binds outside the project.
    private static ProjectContext CreateProject(params ProjectMount[] extra)
    {
        var fileSystem = new MockFileSystem();
        _ = fileSystem.Directory.CreateDirectory("C:/Projects/Mounts");
        fileSystem.File.WriteAllText("C:/Projects/Mounts/.mounts.json", $$$"""{"schema_version":{{{ProjectMounts.SchemaVersion}}},"mounts":{"StudioLibrary":"D:/Studio/EnvironmentLibrary"}}""");
        return TestProject.Create(fileSystem, "C:/Projects/Mounts", [new("Content", MountKind.Source, "Content"), new("StudioLibrary", MountKind.Cooked), .. extra]).Context with
        {
            Name = "Mounts",
            OutputFolderViews = [new(ProjectOutputFolders.Cooked)],
        };
    }
}
