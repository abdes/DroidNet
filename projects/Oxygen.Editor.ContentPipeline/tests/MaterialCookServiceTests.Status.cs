// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Material document state consumes the same read-only cook facts as other surfaces.</summary>
public sealed partial class MaterialCookServiceTests
{
    /// <summary>Opening a material preserves its published state and keeps unsaved input out of Current.</summary>
    /// <param name="freshness">The saved-input state.</param>
    /// <param name="published">Whether prior output is known.</param>
    /// <param name="verified">Whether prior output is intact.</param>
    /// <param name="dirty">Whether the source owner has unsaved edits.</param>
    /// <param name="expected">The material document's cook state.</param>
    /// <returns>The asynchronous document-state regression.</returns>
    [TestMethod]
    [DataRow(AssetCookFreshness.Current, true, true, false, MaterialCookState.Cooked)]
    [DataRow(AssetCookFreshness.Current, true, true, true, MaterialCookState.Stale)]
    [DataRow(AssetCookFreshness.OutOfDate, true, true, false, MaterialCookState.Stale)]
    [DataRow(AssetCookFreshness.OutOfDate, true, false, false, MaterialCookState.Failed)]
    [DataRow(AssetCookFreshness.InvalidSource, true, true, false, MaterialCookState.Failed)]
    [DataRow(AssetCookFreshness.NeedsCooking, false, false, false, MaterialCookState.NotCooked)]
    public async Task ReadMaterialStateUsesSharedCookFacts(AssetCookFreshness freshness, bool published, bool verified, bool dirty, MaterialCookState expected)
    {
        using var workspace = new TempWorkspace();
        var uri = new Uri("asset:///Content/Material.omat.json");
        var source = Path.Combine(workspace.Root, "Content", "Material.omat.json");
        var state = new AssetCookStatus(
            uri,
            freshness,
            published,
            verified,
            [],
            dirty ? [new CookDocumentState(Guid.NewGuid(), source, "Material", 2, 1, IsDirty: true, new string('A', 64))] : [],
            []);
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        _ = pipeline.Setup(service => service.ReadAsync(workspace.ContextService.ActiveProject!, It.Is<IReadOnlyList<Uri>>(uris => uris.Count == 1 && uris[0] == uri), It.IsAny<CancellationToken>()))
            .ReturnsAsync([state]);
        var service = new MaterialCookService(pipeline.Object, workspace.ContextService, NullLogger<MaterialCookService>.Instance);
        var result = await service.GetMaterialCookStateAsync(uri, CancellationToken.None).ConfigureAwait(false);
        _ = result.Should().Be(expected);
        pipeline.VerifyAll();
    }
}
