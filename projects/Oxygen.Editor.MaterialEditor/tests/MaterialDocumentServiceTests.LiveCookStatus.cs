// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Checks open material status against real publication and the shared browser/picker feed.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Cooks from outside the material editor update it without reload or an extra cook.</summary>
    /// <param name="scope">The external cook entry point.</param>
    /// <returns>The asynchronous native publication regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("Automatic")]
    [DataRow("Folder")]
    [DataRow("Project")]
    public async Task ExternalCooksUpdateOpenMaterialAndPreserveLaterEdits(string scope)
    {
        using var workspace = new TempWorkspace();
        var service = CreateCookingService(workspace);
        var uri = new Uri("asset:///Content/Materials/Live.omat.json");
        var created = await service.CreateAsync(uri, this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false, this.TestContext.CancellationToken).ConfigureAwait(false);
        var pipeline = workspace.NativePipeline.Pipeline;
        var catalog = new Mock<IProjectAssetCatalog>();
        _ = catalog.SetupGet(value => value.Changes).Returns(Observable.Empty<AssetChange>());
        _ = catalog.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        _ = catalog.Setup(value => value.QueryAsync(It.IsAny<AssetQuery>(), It.IsAny<CancellationToken>())).ReturnsAsync([new AssetRecord(uri)]);
        using var provider = new ContentBrowserAssetProvider(
            catalog.Object,
            workspace.ContextService,
            new ProjectCookScopeProvider(new NativeStorageProvider(new Testably.Abstractions.RealFileSystem())),
            new AssetIdentityReducer(),
            pipeline,
            workspace.CookDocuments,
            workspace.CookCoordinator);
        using var picker = new MaterialPickerService(provider);
        using var editor = new MaterialEditorViewModel(new MaterialDocumentMetadata(uri), service, provider, ImmediateScheduler.Instance);
        await WaitForMaterialUiAsync(() => editor.IsLoaded && string.Equals(editor.CookStatusText, "Needs cooking", StringComparison.Ordinal), this.TestContext.CancellationToken).ConfigureAwait(false);
        var initialCookCount = workspace.CookCoordinator.Runs.Count;
        _ = initialCookCount.Should().Be(0, "opening the editor only observes saved status");

        var result = await CookScopeAsync().ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join("; ", result.Diagnostics.Select(static diagnostic => diagnostic.Message)));
        await WaitForMaterialUiAsync(() => string.Equals(editor.CookStatusText, "Cooked", StringComparison.Ordinal), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = editor.AssetStatus!.CookStatus!.HasVerifiedOutput.Should().BeTrue();
        _ = workspace.CookCoordinator.Runs.Should().HaveCount(initialCookCount + 1);
        var choice = await picker.ResolveAsync(uri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = choice!.StatusText.Should().Be(editor.CookStatusText);

        editor.RoughnessFactor = 0.73f;
        await WaitForMaterialUiAsync(() => editor.IsDirty, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = editor.CookStatusText.Should().Be("Unsaved changes");
        _ = editor.AssetStatus!.CookStatus!.HasVerifiedOutput.Should().BeTrue("a later edit does not erase prior published output");
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = editor.CookStatusText.Should().Be("Unsaved changes");
        _ = editor.RoughnessFactor.Should().Be(0.73f);
        _ = editor.UndoCommand.CanExecute(parameter: null).Should().BeTrue();
        await editor.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = editor.IsDirty.Should().BeFalse();
        _ = editor.CookStatusText.Should().Be("Cooked");
        _ = workspace.CookCoordinator.Runs.Should().HaveCount(initialCookCount + 1, "editing, status updates and Undo never cook");
        await this.VerifyMaterialSaveAndRecookAsync(editor, service, provider, uri, CookScopeAsync).ConfigureAwait(false);
        _ = workspace.CookCoordinator.Runs.Should().HaveCount(initialCookCount + 2);

        Task<ContentCookResult> CookScopeAsync() => scope switch
        {
            "Automatic" => pipeline.CookSavedAssetAsync(uri, workspace.ContextService.ActiveProject!, this.TestContext.CancellationToken),
            "Folder" => pipeline.CookFolderAsync(new Uri("asset:///Content/Materials"), this.TestContext.CancellationToken),
            _ => pipeline.CookProjectAsync(this.TestContext.CancellationToken),
        };
    }

    private async Task VerifyMaterialSaveAndRecookAsync(
        MaterialEditorViewModel editor,
        IMaterialDocumentService service,
        IContentBrowserAssetProvider provider,
        Uri uri,
        Func<Task<ContentCookResult>> cook)
    {
        editor.RoughnessFactor = 0.31f;
        await editor.SaveAsync().ConfigureAwait(false);
        await WaitForMaterialUiAsync(() => string.Equals(editor.CookStatusText, "Out of date", StringComparison.Ordinal), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = editor.CookStatusDescription.Should().Contain("Cooking is needed").And.Contain("Previously cooked content");
        var updated = await cook().ConfigureAwait(false);
        _ = updated.IsPublished.Should().BeTrue(string.Join("; ", updated.Diagnostics.Select(static diagnostic => diagnostic.Message)));
        await WaitForMaterialUiAsync(() => string.Equals(editor.CookStatusText, "Cooked", StringComparison.Ordinal), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var reopened = new MaterialEditorViewModel(new MaterialDocumentMetadata(uri), service, provider, ImmediateScheduler.Instance);
        await WaitForMaterialUiAsync(() => reopened.IsLoaded, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reopened.CookStatusText.Should().Be("Cooked");
        _ = reopened.RoughnessFactor.Should().Be(0.31f);
    }
}
