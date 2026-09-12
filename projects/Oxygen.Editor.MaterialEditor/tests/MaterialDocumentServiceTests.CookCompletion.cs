// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Preserves newer authored material state when an earlier cook finishes.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Reopening a cooked material preserves its known output state without submitting another cook.</summary>
    /// <returns>The asynchronous native material workflow.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ReopeningCookedMaterialPreservesItsStateWithoutCooking()
    {
        using var workspace = new TempWorkspace();
        var service = CreateCookingService(workspace);
        var uri = new Uri("asset:///Content/Materials/Test.omat.json");
        var document = await service.CreateAsync(uri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var cooked = await service.CookAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cooked.State.Should().Be(MaterialCookState.Cooked);
        await service.CloseAsync(document.DocumentId, discard: false, this.TestContext.CancellationToken).ConfigureAwait(false);
        var count = workspace.CookCoordinator.Runs.Count;

        var reopened = await service.OpenAsync(uri, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reopened.CookState.Should().Be(MaterialCookState.Cooked);
        _ = workspace.CookCoordinator.Runs.Count.Should().Be(count);
        _ = reopened.IsDirty.Should().BeFalse();
    }

    /// <summary>Neither an unsaved edit nor a newer completed save can be marked current by an older cook.</summary>
    /// <param name="saveLater">Whether to save the newer edit before the old cook completes.</param>
    /// <returns>The asynchronous document test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task LaterMaterialEditsRemainStaleAtCookCompletion(bool saveLater)
    {
        using var workspace = new TempWorkspace();
        var completion = new TaskCompletionSource<MaterialCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var cookService = new RecordingCookService { Completion = completion.Task };
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), cookService, workspace.CookDocuments, CreateFileStore());
        var uri = new Uri("asset:///Content/Materials/Test.omat.json");
        var created = await service.CreateAsync(uri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var cooking = service.CookAsync(created.DocumentId, this.TestContext.CancellationToken);
        _ = await service.EditScalarAsync(created.DocumentId, new(MaterialFieldKeys.MetallicFactor, 0.75f), cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        if (saveLater)
        {
            _ = (await service.SaveAsync(created.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        }

        completion.SetResult(new(uri, new("asset:///Content/Materials/Test.omat"), MaterialCookState.Cooked, Guid.NewGuid()));
        var result = await cooking.ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Stale);
        var current = service.GetDocument(created.DocumentId);
        _ = current.CookState.Should().Be(MaterialCookState.Stale);
        _ = current.IsDirty.Should().Be(!saveLater);
        _ = current.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.75f);
    }
}
