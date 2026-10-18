// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Checks shared progress, failures and document lifetime without changing authoring state.</summary>
public sealed partial class MaterialEditorViewModelTests
{
    /// <summary>Background status retains old output facts and cannot clear newer edits or save errors.</summary>
    /// <returns>The asynchronous view-model regression.</returns>
    [TestMethod]
    public async Task SharedStatusPreservesAuthoredValuesAndSaveErrors()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        var item = CreateStatusItem(document.File.Asset);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([item]);
        var provider = new Mock<IAssetFeed>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var editor = materials.EditorOf(document, provider.Object);
        _ = editor.CookStatusText.Should().Be("Cooked");
        foreach (var (state, text) in new[] { (CookRunState.Queued, "Queued"), (CookRunState.Cooking, "Cooking"), (CookRunState.Publishing, "Updating preview"), (CookRunState.Failed, "Cook failed"), (CookRunState.Cancelled, "Cooked") })
        {
            updates.OnNext([item with { CookActivity = new(Guid.NewGuid(), state) }]);
            _ = editor.CookStatusText.Should().Be(text);
            _ = editor.AssetStatus!.CookStatus!.HasAvailableOutput.Should().BeTrue();
        }

        editor.RoughnessFactor = 0.73f;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);
        editor.StatusText = "The file changed outside this document.";
        updates.OnNext([item with { CookActivity = new(Guid.NewGuid(), CookRunState.Succeeded) }]);
        _ = editor.CookStatusText.Should().Be("Unsaved changes");
        _ = editor.RoughnessFactor.Should().Be(0.73f);
        _ = editor.StatusText.Should().Be("The file changed outside this document.");
        _ = document.History.UndoStack.Should().ContainSingle();
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
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([]);
        var provider = new Mock<IAssetFeed>();
        var detached = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>()))
            .Returns(async (CancellationToken token) =>
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
        using var editor = materials.EditorOf(document, provider.Object);
        _ = editor.CookStatusText.Should().Be("Checking status");
        editor.Dispose();
        await detached.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = updates.HasObservers.Should().BeFalse();
        updates.OnNext([CreateStatusItem(document.File.Asset)]);
        _ = editor.AssetStatus.Should().BeNull();
    }

    private static AssetEntry CreateStatusItem(AssetPath uri) => new(
        uri, "Test", AssetKind.Material, AssetState.Descriptor, AssetState.Cooked, AssetRuntimeAvailability.Unknown, uri.VirtualPath, SourcePath: null, DescriptorPath: null, CookedPath: null, AssetGuid: null, DiagnosticCodes: [], IsSelectable: true)
    {
        CookStatus = new(uri, AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], [], []),
    };
}
