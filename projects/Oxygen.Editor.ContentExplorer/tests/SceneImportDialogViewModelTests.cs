// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using Moq;
using Oxygen.Editor.ContentExplorer.Importing;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>Verifies the reviewable model import destination and inline validation.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository configuration.")]
public sealed class SceneImportDialogViewModelTests
{
    /// <summary>Gets or sets the test cancellation context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A valid review produces a source request with the user's visible name and destination.</summary>
    [TestMethod]
    public void ValidReviewProducesMatchingRequest()
    {
        var project = CreateProject();
        var model = new SceneImportDialogViewModel(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "/Content/Models", Mock.Of<IDialogService>());
        _ = model.CanAccept.Should().BeTrue();
        _ = model.Request!.Name.Should().Be("Crate");
        _ = model.Request.DestinationFolder.Should().Be(AssetPath.Parse("/Content/Models"));
        _ = model.SourceLocation.Should().Contain("/Content/Models/Crate");
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
            var project = CreateProject() with { ProjectRoot = FileSystemPath.From(root.FullName) };
            _ = Directory.CreateDirectory(Path.Combine(root.FullName, "Content/Models/Crate"));
            var model = new SceneImportDialogViewModel(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "/Content/Models", Mock.Of<IDialogService>());
            _ = model.CanAccept.Should().BeFalse();
            _ = model.HasCollision.Should().BeTrue();
            _ = model.ReplacementTitle.Should().Contain("already exists");
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
        var model = new SceneImportDialogViewModel(project, Path.Combine(project.ProjectRoot.FullPath, "Content/Models/Crate/Crate.fbx"), "/Content/Models", Mock.Of<IDialogService>());
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
        _ = dialogs.Setup(value => value.PickFolderAsync(It.IsAny<FolderPickerSpec>(), It.IsAny<CancellationToken>())).ReturnsAsync(Path.GetTempPath());
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
        _ = dialogs.Setup(value => value.PickFolderAsync(It.IsAny<FolderPickerSpec>(), It.IsAny<CancellationToken>())).ThrowsAsync(new IOException("Unavailable"));
        var model = new SceneImportDialogViewModel(CreateProject(), Path.Combine(Path.GetTempPath(), "Crate.gltf"), "/Content/Models", dialogs.Object);
        await model.BrowseDestinationCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = model.Error.Should().Contain("Unavailable");
        _ = model.DestinationFolder.Should().Be("/Content/Models");
        _ = model.Validate().Should().BeTrue();
    }

    /// <summary>Replacement is explicit, identifies its target, and is reset by changing the review.</summary>
    /// <returns>The asynchronous collision review regression.</returns>
    [TestMethod]
    public async Task ReviewedReplacementPreservesExistingDestination()
    {
        var directory = Directory.CreateTempSubdirectory("OxygenReplacementReview-");
        try
        {
            var project = CreateProject() with { ProjectRoot = FileSystemPath.From(directory.FullName) };
            var root = Path.Combine(project.ProjectRoot.FullPath, "Content/Models/Crate");
            _ = Directory.CreateDirectory(root);
            var bytes = System.Text.Encoding.UTF8.GetBytes("original source");
            await File.WriteAllBytesAsync(Path.Combine(root, "model.gltf"), bytes, this.TestContext.CancellationToken).ConfigureAwait(false);
            var recordPath = ModelImportSettings.PathFor(project.ProjectRoot.FullPath, Guid.CreateVersion7());
            _ = Directory.CreateDirectory(Path.GetDirectoryName(recordPath)!);
            var record = ModelImportSettings.Create(recordPath, Path.Combine(root, "model.gltf"), "/Content/Models/Crate/model.gltf", ModelImportSettings.DefaultSettings("Crate"));
            await File.WriteAllBytesAsync(recordPath, record.ToBytes(), this.TestContext.CancellationToken).ConfigureAwait(false);
            var model = new SceneImportDialogViewModel(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "/Content/Models", Mock.Of<IDialogService>());
            _ = model.CanAccept.Should().BeFalse();
            _ = model.Request.Should().BeNull();
            await model.ReviewReplacementCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
            _ = model.CanAccept.Should().BeTrue();
            _ = model.PrimaryButtonText.Should().Be("Replace and import");
            _ = model.Request!.Replacement!.SourceUri.Should().Be(AssetPath.Parse("/Content/Models/Crate/model.gltf"));
            _ = model.DestinationFolder.Should().Be("/Content/Models");
            _ = model.ReplacementMessage.Should().Be("/Content/Models/Crate/model.gltf");
            _ = model.ModelLocation.Should().Be("Model: /Content/Models/Crate/model.gltf");
            model.Name = "Another";
            _ = model.Replacement.Should().BeNull();
            _ = model.PrimaryButtonText.Should().Be("Import");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    private static ProjectContext CreateProject()
        => TestProject.Create(new RealFileSystem(), Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"))).Context with { Name = "Import" };
}
