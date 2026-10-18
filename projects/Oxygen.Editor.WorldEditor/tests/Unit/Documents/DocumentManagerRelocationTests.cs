// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World.Documents;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Open material and scene documents follow the deletes and moves the editor makes.</summary>
[TestClass]
public sealed class DocumentManagerRelocationTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A deleted material's document closes; a moved one keeps its tab.</summary>
    /// <param name="deleted">Whether the material was deleted rather than moved.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task MaterialDocumentClosesOnlyWhenDeleted(bool deleted)
    {
        using var files = new SceneDocumentFiles();
        var opening = await files.Documents.OpenAsync(files.WriteMaterial("Red"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var material = opening.Document!;
        var relocation = new Mock<IAssetRelocationService>();
        var (tabs, area) = CreateArea(files, new WindowId(91));
        using var manager = CreateManager(files, area, relocation.Object);
        _ = (await area.ShowAsync(material).ConfigureAwait(false)).Should().BeTrue();
        var source = material.File.Path;
        var target = source.Parent!.Combine("Blue.omat.json");
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        tabs.DocumentClosed += (_, _) => _ = closed.TrySetResult();

        var args = deleted
            ? new AssetFilesChangedEventArgs([], [], [source], foldersChanged: false)
            : new AssetFilesChangedEventArgs([new RelocationFileMove(source, target, IsDirectory: false)], [], [], foldersChanged: false);
        relocation.Raise(value => value.FilesChanged += null, args);
        if (deleted)
        {
            await closed.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        _ = area.Documents.Should().HaveCount(deleted ? 0 : 1);
        area.Dispose();
    }

    /// <summary>Deleting a scene's folder in the editor closes the scene's tab; the delete already asked about it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SceneDocumentClosesWhenItsFileIsDeleted()
    {
        using var files = new SceneDocumentFiles();
        var (document, _) = await files.CreateAsync("Main", this.TestContext.CancellationToken).ConfigureAwait(false);
        var relocation = new Mock<IAssetRelocationService>();
        var (tabs, area) = CreateArea(files, new WindowId(92));
        using var manager = CreateManager(files, area, relocation.Object);
        _ = (await area.ShowAsync(document).ConfigureAwait(false)).Should().BeTrue();
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        tabs.DocumentClosed += (_, _) => _ = closed.TrySetResult();

        relocation.Raise(value => value.FilesChanged += null, new AssetFilesChangedEventArgs([], [], [document.File.Path.Parent!], foldersChanged: true));
        await closed.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = area.Documents.Should().BeEmpty();
        area.Dispose();
    }

    private static (EditorDocumentService Tabs, DocumentArea Area) CreateArea(SceneDocumentFiles files, WindowId window)
    {
        var tabs = new EditorDocumentService();
        var area = new DocumentArea(
            tabs,
            files.Documents,
            files.Documents.Kinds.Select(static kind => new DocumentEditorDescriptor(kind, static _ => new object())),
            window,
            static _ => { });
        return (tabs, area);
    }

    private static DocumentManager CreateManager(SceneDocumentFiles files, DocumentArea area, IAssetRelocationService relocation)
    {
        var projects = new ProjectContextService();
        projects.Activate(files.Context);
        return new DocumentManager(
            area,
            new StrongReferenceMessenger(),
            projects,
            Mock.Of<IProjectUsageService>(),
            new WindowId(90),
            files.Documents,
            relocation);
    }
}
