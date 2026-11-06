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
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Assets.Testing;
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
            ("Engine", "Cooked"));
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

        model.SelectedKind = ContentMountsViewModel.SourceFolderKind;
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
        var project = CreateProject();
        _ = project.FileSystem.Directory.CreateDirectory("C:/Shared/Trees");
        var model = CreateModel(project);
        model.SelectedKind = ContentMountsViewModel.SourceFolderKind;
        model.NewMountName = "Trees";
        model.NewMountPath = "C:/Shared/Trees";
        model.AddMountCommand.Execute(parameter: null);
        var added = model.Mounts.Single(static row => row.Name == "Trees");

        added.RenameCommand.Execute(parameter: null);
        _ = added.IsRenaming.Should().BeTrue();
        added.NameDraft = "Content";
        _ = model.Validate().Should().BeFalse();
        _ = model.ErrorMessage.Should().Be("A mount with this name already exists, or differs from one only in case.");

        added.NameDraft = "Published";
        _ = model.Validate().Should().BeTrue();
        _ = added.Name.Should().Be("Published");
        _ = added.IsRenaming.Should().BeFalse();
    }

    /// <summary>A library has no rename: its name is what it holds.</summary>
    [TestMethod]
    public void LibrariesCannotBeRenamed()
    {
        var library = CreateModel(CreateProject()).Mounts.Single(static row => row.Name == "StudioLibrary");

        _ = library.CanRename.Should().BeFalse();
        _ = library.IsRenameVisible.Should().BeFalse();
        _ = library.RenameCommand.CanExecute(parameter: null).Should().BeFalse();
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
        model.SelectedKind = ContentMountsViewModel.SourceFolderKind;
        model.NewMountName = "Art";
        model.NewMountPath = "C:/Libraries/Library";
        model.AddMountCommand.Execute(parameter: null);
        _ = model.HasError.Should().BeFalse(model.ErrorMessage);

        var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
        ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts);

        _ = candidate.Mounts.Should().Equal(new ProjectMount("Content", MountKind.Source, "Content"), new ProjectMount("Art", MountKind.Source, "../../Libraries/Library"));
        _ = candidate.OutputFolderViews.Select(static view => view.Folder).Should().Equal(ProjectOutputFolders.Cooked, ProjectOutputFolders.Build);
        _ = model.GetBindingChanges().Changes.Should().BeEquivalentTo(
            new Dictionary<string, string?>(StringComparer.Ordinal) { ["StudioLibrary"] = null },
            "the unmounted library's binding leaves the project's .mounts.json");
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

    /// <summary>A library takes its name from its one root, lands above the engine content, and Apply declares it, never the engine content.</summary>
    [TestMethod]
    public void ALibraryIsNamedByWhatItHolds()
    {
        var project = CreateProject();
        WritePak(project, "C:/Paks/Props.pak", "Props", "/Props/Crate.omat");
        var model = CreateModel(project);
        model.SelectedKind = ContentMountsViewModel.LibraryKind;

        model.NewMountPath = "C:/Paks/Props.pak";

        _ = model.HasError.Should().BeFalse(model.ErrorMessage);
        _ = model.Candidate!.Name.Should().Be("Props");
        _ = model.Candidate.Form.Should().Contain("Pak").And.Contain("1 assets");
        _ = model.AddMountCommand.CanExecute(parameter: null).Should().BeTrue();
        model.AddMountCommand.Execute(parameter: null);
        _ = model.Mounts.Select(static row => row.Kind).TakeLast(2).Should().Equal(ContentMountKind.Library, ContentMountKind.EngineContent);

        var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
        ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts);

        _ = candidate.Mounts.Single(static mount => mount.Name == "Props").Kind.Should().Be(MountKind.Cooked);
        _ = candidate.Mounts.Select(static mount => mount.Name).Should().NotContain(AssetPath.Engine.Mount);
    }

    /// <summary>What cannot be a library is refused with its code, and nothing can be added.</summary>
    /// <param name="content">The bad file: its bytes.</param>
    /// <param name="code">The finding.</param>
    [TestMethod]
    [DataRow("not a pak", MountCodes.CookedFormatUnsupported)]
    public void AFileThatIsNotALibraryIsRefused(string content, string code)
    {
        var project = CreateProject();
        _ = project.FileSystem.Directory.CreateDirectory("C:/Paks");
        project.FileSystem.File.WriteAllText("C:/Paks/Bad.pak", content);
        var model = CreateModel(project);
        model.SelectedKind = ContentMountsViewModel.LibraryKind;

        model.NewMountPath = "C:/Paks/Bad.pak";

        _ = model.ErrorMessage.Should().StartWith(code);
        _ = model.Candidate.Should().BeNull();
        _ = model.AddMountCommand.CanExecute(parameter: null).Should().BeFalse();
    }

    /// <summary>A folder holding two roots has no one name.</summary>
    [TestMethod]
    public void AFolderWithSeveralRootsIsRefused()
    {
        var project = CreateProject();
        _ = project.FileSystem.Directory.CreateDirectory("C:/Libs/Mixed");
        project.FileSystem.File.WriteAllBytes("C:/Libs/Mixed/container.index.bin", CookedLibraryFixture.LooseIndex("/Props/A.omat", "/Vehicles/B.omat"));
        var model = CreateModel(project);
        model.SelectedKind = ContentMountsViewModel.LibraryKind;

        model.NewMountPath = "C:/Libs/Mixed";

        _ = model.ErrorMessage.Should().StartWith(MountCodes.CookedRootMultiple);
        _ = model.Candidate.Should().BeNull();
    }

    /// <summary>A second mount of one root is the same library.</summary>
    [TestMethod]
    public void ALibraryWhoseNameIsTakenIsRefused()
    {
        var project = CreateProject();
        WritePak(project, "C:/Paks/StudioLibrary.pak", "StudioLibrary", "/StudioLibrary/Crate.omat");
        var model = CreateModel(project);
        model.SelectedKind = ContentMountsViewModel.LibraryKind;

        model.NewMountPath = "C:/Paks/StudioLibrary.pak";

        _ = model.ErrorMessage.Should().StartWith(MountCodes.NameDuplicate);
        _ = model.Candidate.Should().BeNull();
    }

    /// <summary>A library that needs one not mounted warns and can be added, so libraries can be added in any order.</summary>
    [TestMethod]
    public void AMissingNeedWarnsAndAdds()
    {
        var project = CreateProject();
        _ = project.FileSystem.Directory.CreateDirectory("C:/Paks");
        WritePak(project, "C:/Paks/Props.pak", "Props", "/Props/Crate.omat", ("Vehicles", "/Vehicles/Paint.omat"), ("Engine", "/Engine/Base.omat"));
        var model = CreateModel(project);
        model.SelectedKind = ContentMountsViewModel.LibraryKind;

        model.NewMountPath = "C:/Paks/Props.pak";

        _ = model.HasError.Should().BeFalse(model.ErrorMessage);
        _ = model.HasUnmetNeed.Should().BeTrue();
        _ = model.Candidate!.UnmetText.Should().Contain("Vehicles").And.Contain("Invalid");
        _ = model.Candidate.Needs.Should().BeEquivalentTo([new LibraryNeed("Engine", "1 assets · provided", IsMet: true), new LibraryNeed("Vehicles", "1 assets · not mounted", IsMet: false)]);
        _ = model.Candidate.PriorityText.Should().Contain("Above Vehicles and Engine");
        _ = model.AddMountCommand.CanExecute(parameter: null).Should().BeTrue();
    }

    /// <summary>A library lands above what it needs and below what needs it, whichever was added first.</summary>
    [TestMethod]
    public void ANewLibraryLandsBetweenWhatItNeedsAndWhatNeedsIt()
    {
        var project = CreateProject();
        WritePak(project, "C:/Paks/Props.pak", "Props", "/Props/Crate.omat", ("Vehicles", "/Vehicles/Paint.omat"));
        WritePak(project, "C:/Paks/Vehicles.pak", "Vehicles", "/Vehicles/Paint.omat");
        var model = CreateModel(project);
        model.SelectedKind = ContentMountsViewModel.LibraryKind;
        model.NewMountPath = "C:/Paks/Props.pak";
        model.AddMountCommand.Execute(parameter: null);
        model.NewMountPath = "C:/Paks/Vehicles.pak";
        _ = model.HasUnmetNeed.Should().BeFalse();
        model.AddMountCommand.Execute(parameter: null);

        var candidate = new ProjectInfo(project.Name, project.Category, project.ProjectRoot.FullPath);
        ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts);

        var names = candidate.CookedContentOrder.Select(static source => source.Name).ToList();
        _ = names.IndexOf("Vehicles").Should().BeLessThan(names.IndexOf("Props"));
        CookedContentOrdering.DeclareInOrder(candidate);
        var declared = candidate.Mounts.Select(static mount => mount.Name).ToList();
        _ = declared.IndexOf("Vehicles").Should().BeLessThan(declared.IndexOf("Props"), "a table resolves a library's needs against the mounts declared before it");
    }

    /// <summary>A library's place is a fact of each machine: outside the project it starts there, inside it starts in Project.oxy.</summary>
    [TestMethod]
    public void ALibraryIsSavedInTheProjectOnlyFromInsideIt()
    {
        var project = CreateProject();
        WritePak(project, "C:/Paks/Props.pak", "Props", "/Props/Crate.omat");
        WritePak(project, "C:/Projects/Mounts/Paks/Vehicles.pak", "Vehicles", "/Vehicles/Paint.omat");
        var model = CreateModel(project);
        model.SelectedKind = ContentMountsViewModel.LibraryKind;

        model.NewMountPath = "C:/Paks/Props.pak";
        _ = model.Placement.CanSaveInProject.Should().BeTrue();
        _ = model.Placement.SaveOnThisMachine.Should().BeTrue("it lies outside the project");

        model.NewMountPath = "C:/Projects/Mounts/Paks/Vehicles.pak";
        _ = model.Placement.SaveInProject.Should().BeTrue("the repository holds it");
    }

    /// <summary>An Invalid library says why, and has only the actions that can fix it.</summary>
    [TestMethod]
    public void AnInvalidRowShowsItsFinding()
    {
        var project = CreateProject(new ProjectMount("Kit", MountKind.Cooked, "../../Paks/Props.pak"));
        WritePak(project, "C:/Paks/Props.pak", "Props", "/Props/Crate.omat");
        var model = CreateModel(TestProject.Open(TestProject.InfoOf(project), project.FileSystem).Context);

        var row = model.Mounts.Single(static row => row.Name == "Kit");

        _ = row.Binding.Should().Be(MountBindingState.Invalid);
        _ = row.ShowsInvalid.Should().BeTrue();
        _ = row.BindingText.Should().Be("Invalid: holds another root");
        _ = row.BindingDetail.Should().Be(MountCodes.CookedRootMismatch);
        _ = row.BindingToolTip.Should().Contain("Props");
        _ = row.UnmountCommand.CanExecute(parameter: null).Should().BeTrue();
    }

    /// <summary>The content priority keeps a library above what it needs.</summary>
    [TestMethod]
    public void ThePriorityRefusesAMoveBelowANeed()
    {
        var project = CreateProject(new ProjectMount("Vehicles", MountKind.Cooked, "../../Paks/Vehicles.pak"), new ProjectMount("Props", MountKind.Cooked, "../../Paks/Props.pak"));
        WritePak(project, "C:/Paks/Vehicles.pak", "Vehicles", "/Vehicles/Paint.omat");
        WritePak(project, "C:/Paks/Props.pak", "Props", "/Props/Crate.omat", ("Vehicles", "/Vehicles/Paint.omat"));
        var opened = TestProject.Open(TestProject.InfoOf(project), project.FileSystem).Context;
        var model = new ContentPriorityViewModel(opened);

        model.SelectedSource = model.Sources.Single(static source => source.Source.Name == "Props");
        _ = model.DecreasePriorityCommand.CanExecute(parameter: null).Should().BeFalse();

        model.SelectedSource = model.Sources.Single(static source => source.Source.Name == "Vehicles");
        _ = model.IncreasePriorityCommand.CanExecute(parameter: null).Should().BeFalse();
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
        model.SelectedKind = ContentMountsViewModel.SourceFolderKind;

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
        ContentFoldersViewModel.ApplyMountRows(project, candidate, model.Mounts);
        _ = candidate.Mounts.Should().Contain(new ProjectMount("Trees", MountKind.Source, "../../Shared/Trees"));
        _ = candidate.Mounts.Should().Contain(new ProjectMount("Rocks", MountKind.Source, Path: null));
        _ = model.GetBindingChanges().Changes.Should().BeEquivalentTo(new Dictionary<string, string?>(StringComparer.Ordinal) { ["Rocks"] = "E:/Shared/Rocks" });
    }

    private static ContentMountsViewModel CreateModel(ProjectContext project) => new(project, Mock.Of<IDialogService>(), EnginePak);

    private static void WritePak(ProjectContext project, string path, string library, string asset, params (string Library, string Asset)[] needs)
    {
        _ = project.FileSystem.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = new MemoryStream();
        PakFixture.Write(stream, new PakDocument(1, Guid.CreateVersion7(), new string('0', 64), [new PakAssetEntry(AssetKey.FromVirtualPath(asset), (byte)AssetType.Material, asset, 0, 0)])
        {
            Library = library,
            Dependencies = [.. needs.GroupBy(static need => need.Library, StringComparer.Ordinal).Select(static group => new LibraryDependency(group.Key, [.. group.Select(static need => new LibraryRequirement(IsTexture: false, AssetKey.FromVirtualPath(need.Asset), (byte)AssetType.Material))]))],
        });
        project.FileSystem.File.WriteAllBytes(path, stream.ToArray());
    }

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
