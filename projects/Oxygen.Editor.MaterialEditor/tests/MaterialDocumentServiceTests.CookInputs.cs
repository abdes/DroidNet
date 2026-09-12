// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Qualifies cook reads against the material document's actual persistence gate.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Waits for an in-flight save and captures its acknowledged revision and content hash.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookReadWaitsForSaveAndCapturesAcknowledgedBytes()
    {
        using var workspace = new TempWorkspace();
        var writing = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var files = new ControlledFileStore(async (_, _, token) =>
        {
            writing.SetResult();
            await release.Task.WaitAsync(token).ConfigureAwait(false);
        });
        var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, files);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Capture.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(document.DocumentId, new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.25f), this.TestContext.CancellationToken).ConfigureAwait(false);
        var save = service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken);
        await writing.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var capture = workspace.CookDocuments.AcquireAsync([document.SourcePath], this.TestContext.CancellationToken);
        _ = capture.IsCompleted.Should().BeFalse();

        release.SetResult();
        _ = (await save.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        using var reads = await capture.ConfigureAwait(false);
        var state = reads.Documents.Should().ContainSingle().Which;
        var bytes = await File.ReadAllBytesAsync(document.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = state.SavedContentHash.Should().Be(Convert.ToHexString(SHA256.HashData(bytes)));
        _ = state.Revision.Should().Be(1);
        _ = state.SavedRevision.Should().Be(1);
        _ = state.IsDirty.Should().BeFalse();
    }

    /// <summary>Prevents save and close from replacing or retiring an input until capture releases it.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookReadBlocksSaveAndCloseUntilReleased()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/HeldCapture.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var reads = await workspace.CookDocuments.AcquireAsync([document.SourcePath], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(document.DocumentId, new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.75f), this.TestContext.CancellationToken).ConfigureAwait(false);
        var save = service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken);
        var close = service.CloseAsync(document.DocumentId, discard: false, this.TestContext.CancellationToken);
        _ = save.IsCompleted.Should().BeFalse();
        _ = close.IsCompleted.Should().BeFalse();

        reads.Dispose();
        _ = (await save.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        await close.ConfigureAwait(false);
        using var afterClose = await workspace.CookDocuments.AcquireAsync([document.SourcePath], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = afterClose.Documents.Should().BeEmpty();
    }

    /// <summary>Reports unsaved gesture previews without completing the gesture or adding history.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookReadSeesUncommittedPreviewWithoutChangingHistory()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/PreviewCapture.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var session = service.BeginEditSession(document.DocumentId, "Roughness");
        _ = await service.PreviewPropertiesAsync(session, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.25f), this.TestContext.CancellationToken).ConfigureAwait(false);

        using var reads = await workspace.CookDocuments.AcquireAsync([document.SourcePath], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reads.Documents.Should().ContainSingle().Which.IsDirty.Should().BeTrue();
        _ = service.GetDocument(document.DocumentId).Revision.Should().Be(0);
        _ = service.CompleteEditSession(session, commit: false).Succeeded.Should().BeTrue();
        _ = service.GetDocument(document.DocumentId).Source.Should().BeEquivalentTo(document.Source);
        _ = service.CanUndo(document.DocumentId).Should().BeFalse();
    }
}
