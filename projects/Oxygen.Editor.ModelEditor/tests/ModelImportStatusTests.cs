// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ModelEditor.Tests;

/// <summary>
/// Where a model's import stands, as the toolbar's status says it (model editor 5.1): read from the cook state of the
/// model's scene product, and <em>Reimport</em> enabled when the source changed or the last import failed.
/// </summary>
[TestClass]
public sealed class ModelImportStatusTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Car.glb");

    [TestMethod]
    public void Of_NoSceneProduct_IsNotImported()
        => _ = ModelImportStatus.Of(Car, []).Should().Be(ModelImportState.NotImported);

    [TestMethod]
    [DataRow(AssetCookFreshness.Current, ModelImportState.Imported)]
    [DataRow(AssetCookFreshness.OutOfDate, ModelImportState.SourceChanged)]
    [DataRow(AssetCookFreshness.MissingSource, ModelImportState.SourceMissing)]
    [DataRow(AssetCookFreshness.InvalidSource, ModelImportState.ImportFailed)]
    [DataRow(AssetCookFreshness.NeedsCooking, ModelImportState.NotImported)]
    public void Of_IdleProduct_FollowsItsFreshness(AssetCookFreshness freshness, ModelImportState expected)
        => _ = ModelImportStatus.Of(Car, [ProductOf(Car, freshness)]).Should().Be(expected);

    [TestMethod]
    [DataRow(CookRunState.Queued)]
    [DataRow(CookRunState.Cooking)]
    [DataRow(CookRunState.Publishing)]
    public void Of_ProductInARun_IsImporting(CookRunState state)
        => _ = ModelImportStatus.Of(Car, [ProductOf(Car, AssetCookFreshness.OutOfDate) with { CookActivity = new AssetCookActivity(Guid.NewGuid(), state) }])
            .Should().Be(ModelImportState.Importing);

    [TestMethod]
    public void Of_ProductWhoseRunFailed_IsImportFailed()
        => _ = ModelImportStatus.Of(Car, [ProductOf(Car, AssetCookFreshness.Current) with { CookActivity = new AssetCookActivity(Guid.NewGuid(), CookRunState.Failed) }])
            .Should().Be(ModelImportState.ImportFailed);

    [TestMethod]
    [DataRow(ModelImportState.SourceChanged, true)]
    [DataRow(ModelImportState.ImportFailed, true)]
    [DataRow(ModelImportState.Imported, false)]
    [DataRow(ModelImportState.Importing, false)]
    [DataRow(ModelImportState.NotImported, false)]
    [DataRow(ModelImportState.SourceMissing, false)]
    public void CanReimport_OnlyWhenTheSourceChangedOrTheImportFailed(ModelImportState state, bool expected)
        => _ = ModelImportStatus.CanReimport(state).Should().Be(expected);

    [TestMethod]
    public void TextOf_SaysEveryState()
        => _ = Enum.GetValues<ModelImportState>().Select(ModelImportStatus.TextOf).Should().OnlyHaveUniqueItems().And.NotContain(string.Empty);

    private static AssetEntry ProductOf(AssetPath source, AssetCookFreshness freshness)
    {
        var product = AssetFileTypes.SceneProductOf(source)!;
        return new AssetEntry(product, product.Name, AssetKind.Scene, AssetState.Cooked, DerivedState: null, AssetRuntimeAvailability.NotApplicable, product.VirtualPath, SourcePath: null, DescriptorPath: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true)
        {
            CookStatus = new AssetCookStatus(product, freshness, HasPublishedOutput: true, CookedOutputAvailability.Present, [], []),
        };
    }
}
