// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using Moq;
using Oxygen.Editor.ContentBrowser.Importing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Verifies the reviewable model import destination and inline validation.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository configuration.")]
public sealed class SceneImportDialogViewModelTests
{
    /// <summary>A valid review produces a source request with the user's visible name and destination.</summary>
    [TestMethod]
    public void ValidReviewProducesMatchingRequest()
    {
        var project = CreateProject();
        var model = new SceneImportDialogViewModel(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "/Content/Models", Mock.Of<IDialogService>());
        _ = model.CanAccept.Should().BeTrue();
        _ = model.Request!.Name.Should().Be("Crate");
        _ = model.Request.DestinationFolder.Should().Be(new Uri("asset:///Content/Models"));
        _ = model.SourceLocation.Should().Contain("/Content/SourceMedia/DCC/Crate");
    }

    /// <summary>Generated and source-media folders are not advertised as import output destinations.</summary>
    /// <param name="folder">An invalid destination.</param>
    [TestMethod]
    [DataRow("/Cooked/Content")]
    [DataRow("/Engine/Materials")]
    [DataRow("/Content/SourceMedia/DCC")]
    [DataRow("/Content/../Outside")]
    public void InvalidDestinationKeepsReviewOpen(string folder)
    {
        var model = new SceneImportDialogViewModel(CreateProject(), Path.Combine(Path.GetTempPath(), "Crate.gltf"), folder, Mock.Of<IDialogService>());
        _ = model.Validate().Should().BeFalse();
        _ = model.Request.Should().BeNull();
        _ = model.Error.Should().NotBeEmpty();
        model.DestinationFolder = "/Content/Models";
        _ = model.Validate().Should().BeTrue();
        _ = model.Error.Should().BeEmpty();
    }

    /// <summary>Existing retained bundles require another name instead of being overwritten.</summary>
    [TestMethod]
    public void SourceCollisionRequiresExplicitNewName()
    {
        var root = Directory.CreateTempSubdirectory("OxygenImportReview-");
        try
        {
            var project = CreateProject() with { ProjectRoot = root.FullName };
            _ = Directory.CreateDirectory(Path.Combine(root.FullName, "Content/SourceMedia/DCC/Crate"));
            var model = new SceneImportDialogViewModel(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "/Content/Models", Mock.Of<IDialogService>());
            _ = model.CanAccept.Should().BeFalse();
            _ = model.Error.Should().Contain("already exists");
            model.Name = "Crate2";
            _ = model.CanAccept.Should().BeTrue();
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }

    /// <summary>In-project source reviews identify that the original source remains in place.</summary>
    [TestMethod]
    public void ProjectSourceIsKeptInPlace()
    {
        var project = CreateProject();
        var model = new SceneImportDialogViewModel(project, Path.Combine(project.ProjectRoot, "Content/SourceMedia/DCC/Crate.fbx"), "/Content/Models", Mock.Of<IDialogService>());
        _ = model.CanAccept.Should().BeTrue();
        _ = model.SourceLocation.Should().Contain("stay");
    }

    /// <summary>An invalid picked folder is visible and cannot silently leave the previous destination accepted.</summary>
    /// <returns>The asynchronous picker validation test.</returns>
    [TestMethod]
    public async Task InvalidPickedFolderRequiresCorrection()
    {
        var project = CreateProject();
        var dialogs = new Mock<IDialogService>();
        _ = dialogs.Setup(value => value.PickFolderAsync(It.IsAny<CancellationToken>())).ReturnsAsync(Path.GetTempPath());
        var model = new SceneImportDialogViewModel(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "/Content/Models", dialogs.Object);
        await model.BrowseDestinationCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = model.CanAccept.Should().BeFalse();
        _ = model.Validate().Should().BeFalse();
        _ = model.Request.Should().BeNull();
        model.DestinationFolder = "/Content/Models";
        _ = model.CanAccept.Should().BeTrue();
    }

    /// <summary>Picker cancellation and failure preserve the editable review.</summary>
    /// <returns>The asynchronous picker failure test.</returns>
    [TestMethod]
    public async Task PickerFailureIsReportedInline()
    {
        var dialogs = new Mock<IDialogService>();
        _ = dialogs.Setup(value => value.PickFolderAsync(It.IsAny<CancellationToken>())).ThrowsAsync(new IOException("Unavailable"));
        var model = new SceneImportDialogViewModel(CreateProject(), Path.Combine(Path.GetTempPath(), "Crate.gltf"), "/Content/Models", dialogs.Object);
        await model.BrowseDestinationCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = model.Error.Should().Contain("Unavailable");
        _ = model.DestinationFolder.Should().Be("/Content/Models");
        _ = model.Validate().Should().BeTrue();
    }

    private static ProjectContext CreateProject() => new()
    {
        ProjectId = Guid.NewGuid(), Name = "Import", Category = Category.Games,
        ProjectRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")),
        AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [],
    };
}
