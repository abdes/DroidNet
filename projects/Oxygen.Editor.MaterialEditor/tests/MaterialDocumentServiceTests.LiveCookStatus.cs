// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Checks that an open material shows the cook status the asset feed publishes for it.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Status published by the feed reaches the open material without a reload, and unsaved edits take precedence until undone.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task OpenMaterialFollowsTheFeedAndItsOwnUnsavedEdits()
    {
        using var workspace = new TempWorkspace();
        var cook = new RecordingCookService();
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), cook, workspace.CookDocuments, CreateFileStore());
        var uri = new Uri("asset:///Content/Materials/Live.omat.json");
        var created = await service.CreateAsync(uri, this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false, this.TestContext.CancellationToken).ConfigureAwait(false);
        var feed = new FakeAssetFeed();
        using var editor = new MaterialEditorViewModel(new MaterialDocumentMetadata(uri), service, feed, ImmediateScheduler.Instance);
        await WaitForMaterialUiAsync(() => editor.IsLoaded, this.TestContext.CancellationToken).ConfigureAwait(false);

        feed.Publish([CreateMaterialEntry(new("asset:///Content/Materials/Other.omat.json"), AssetCookFreshness.Current), CreateMaterialEntry(uri, AssetCookFreshness.NeedsCooking)]);
        _ = editor.CookStatusText.Should().Be("Needs cooking");
        feed.Publish([CreateMaterialEntry(uri, AssetCookFreshness.Current)]);
        _ = editor.CookStatusText.Should().Be("Cooked");

        editor.RoughnessFactor = 0.73f;
        await WaitForMaterialUiAsync(() => editor.IsDirty, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = editor.CookStatusText.Should().Be("Unsaved changes");
        feed.Publish([CreateMaterialEntry(uri, AssetCookFreshness.Current)]);
        _ = editor.CookStatusText.Should().Be("Unsaved changes", "a republished status does not hide unsaved edits");
        await editor.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = editor.IsDirty.Should().BeFalse();
        _ = editor.CookStatusText.Should().Be("Cooked");
        _ = cook.LastRequest.Should().BeNull("editing, status updates and Undo never cook");

        feed.Publish([CreateMaterialEntry(uri, AssetCookFreshness.OutOfDate)]);
        _ = editor.CookStatusText.Should().Be("Out of date");
        _ = editor.CookStatusDescription.Should().Contain("Cooking is needed").And.Contain("Previously cooked content");
        using var reopened = new MaterialEditorViewModel(new MaterialDocumentMetadata(uri), service, feed, ImmediateScheduler.Instance);
        await WaitForMaterialUiAsync(() => reopened.IsLoaded, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reopened.CookStatusText.Should().Be("Out of date", "a newly opened editor shows the feed's latest status");
    }

    private static AssetEntry CreateMaterialEntry(Uri uri, AssetCookFreshness freshness)
    {
        var published = freshness is AssetCookFreshness.Current or AssetCookFreshness.OutOfDate;
        var status = new AssetCookStatus(
            uri,
            freshness,
            published,
            published ? CookedOutputAvailability.Present : CookedOutputAvailability.Unknown,
            [],
            [],
            []);
        return new AssetEntry(
            uri,
            "Material",
            AssetKind.Material,
            published ? AssetState.Cooked : AssetState.Source,
            DerivedState: null,
            AssetRuntimeAvailability.Unknown,
            uri.AbsolutePath,
            SourcePath: null,
            DescriptorPath: null,
            CookedUri: null,
            CookedPath: null,
            AssetGuid: null,
            DiagnosticCodes: [],
            IsSelectable: true)
        {
            CookStatus = status,
        };
    }
}
