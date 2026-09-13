// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using System.Reactive.Subjects;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Checks shared progress, failures and document lifetime without changing authoring state.</summary>
public sealed partial class MaterialEditorViewModelTests
{
    /// <summary>Background status retains old output facts and cannot clear newer edits or save errors.</summary>
    /// <returns>The asynchronous view-model regression.</returns>
    [TestMethod]
    public async Task SharedStatusPreservesAuthoredValuesAndSaveErrors()
    {
        var service = new RecordingDocumentService(CreateDocument());
        var item = CreateStatusItem(service.Document.MaterialUri);
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([item]);
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var editor = new MaterialEditorViewModel(new(service.Document.MaterialUri), service, provider.Object, ImmediateScheduler.Instance);
        await WaitForLoadAsync(editor).ConfigureAwait(false);
        _ = editor.CookStatusText.Should().Be("Cooked", "the document's older local cook enum cannot replace shared status");
        foreach (var (state, text) in new[] { (CookRunState.Queued, "Queued"), (CookRunState.Cooking, "Cooking"), (CookRunState.Publishing, "Updating preview"), (CookRunState.Failed, "Cook failed"), (CookRunState.Cancelled, "Cooked") })
        {
            updates.OnNext([item with { CookActivity = new(Guid.NewGuid(), state) }]);
            _ = editor.CookStatusText.Should().Be(text);
            _ = editor.AssetStatus!.CookStatus!.HasVerifiedOutput.Should().BeTrue();
        }

        editor.RoughnessFactor = 0.73f;
        await WaitForEditAsync(service, expectedCount: 1).ConfigureAwait(false);
        editor.StatusText = "The file changed outside this document.";
        updates.OnNext([item with { CookActivity = new(Guid.NewGuid(), CookRunState.Succeeded) }]);
        _ = editor.CookStatusText.Should().Be("Unsaved changes");
        _ = editor.RoughnessFactor.Should().Be(0.73f);
        _ = editor.StatusText.Should().Be("The file changed outside this document.");
        _ = service.PropertyEditCount.Should().Be(1);
        editor.Dispose();
        _ = updates.HasObservers.Should().BeFalse();
        updates.OnNext([]);
        _ = editor.AssetStatus.Should().NotBeNull("disposed document views cannot receive late status changes");
    }

    /// <summary>Disposing a document cancels only its status wait and leaves the workspace provider usable.</summary>
    /// <returns>The asynchronous disposal regression.</returns>
    [TestMethod]
    public async Task ClosingMaterialDetachesPendingStatusWait()
    {
        var service = new RecordingDocumentService(CreateDocument());
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([]);
        var provider = new Mock<IContentBrowserAssetProvider>();
        var detached = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>()))
            .Returns(async (AssetBrowserFilter _, CancellationToken token) =>
            {
                try
                {
                    await Task.Delay(Timeout.Infinite, token).ConfigureAwait(false);
                }
                finally
                {
                    detached.SetResult();
                }
            });
        using var editor = new MaterialEditorViewModel(new(service.Document.MaterialUri), service, provider.Object, ImmediateScheduler.Instance);
        await WaitForLoadAsync(editor).ConfigureAwait(false);
        _ = editor.CookStatusText.Should().Be("Checking status");
        editor.Dispose();
        await detached.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = updates.HasObservers.Should().BeFalse();
        updates.OnNext([CreateStatusItem(service.Document.MaterialUri)]);
        _ = editor.AssetStatus.Should().BeNull();
    }

    private static ContentBrowserAssetItem CreateStatusItem(Uri uri) => new(
        uri, "Test", AssetKind.Material, AssetState.Descriptor, AssetState.Cooked, AssetRuntimeAvailability.Unknown, uri.AbsolutePath, SourcePath: null, DescriptorPath: null, CookedUri: null, CookedPath: null, AssetGuid: null, DiagnosticCodes: [], IsSelectable: true)
    {
        CookStatus = new(uri, AssetCookFreshness.Current, HasPublishedOutput: true, HasVerifiedOutput: true, [], [], []),
    };
}
