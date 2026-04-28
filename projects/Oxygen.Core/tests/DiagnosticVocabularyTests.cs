// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Core.Diagnostics;

namespace DroidNet.TestHelpers.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Operation Results")]
public class DiagnosticVocabularyTests
{
    [TestMethod]
    public void OperationKinds_ShouldMatchEdM02RuntimeVocabulary()
    {
        _ = RuntimeOperationKinds.Start.Should().Be("Runtime.Start");
        _ = RuntimeOperationKinds.SettingsApply.Should().Be("Runtime.Settings.Apply");
        _ = RuntimeOperationKinds.SurfaceAttach.Should().Be("Runtime.Surface.Attach");
        _ = RuntimeOperationKinds.SurfaceResize.Should().Be("Runtime.Surface.Resize");
        _ = RuntimeOperationKinds.ViewCreate.Should().Be("Runtime.View.Create");
        _ = RuntimeOperationKinds.ViewDestroy.Should().Be("Runtime.View.Destroy");
        _ = RuntimeOperationKinds.ViewSetCameraPreset.Should().Be("Runtime.View.SetCameraPreset");
        _ = RuntimeOperationKinds.CookedRootRefresh.Should().Be("Runtime.CookedRoot.Refresh");
        _ = ViewportOperationKinds.LayoutChange.Should().Be("Viewport.Layout.Change");
    }

    [TestMethod]
    public void DiagnosticCodes_ShouldReserveEdM02Prefixes()
    {
        _ = DiagnosticCodes.RuntimePrefix.Should().Be("OXE.RUNTIME.");
        _ = DiagnosticCodes.SurfacePrefix.Should().Be("OXE.SURFACE.");
        _ = DiagnosticCodes.ViewPrefix.Should().Be("OXE.VIEW.");
        _ = DiagnosticCodes.ViewportPrefix.Should().Be("OXE.VIEWPORT.");
        _ = DiagnosticCodes.SettingsPrefix.Should().Be("OXE.SETTINGS.");
        _ = DiagnosticCodes.AssetMountPrefix.Should().Be("OXE.ASSET_MOUNT.");
    }

    [TestMethod]
    public void OperationKinds_ShouldMatchEdM06AssetIdentityVocabulary()
    {
        _ = AssetOperationKinds.Query.Should().Be("Asset.Query");
        _ = AssetOperationKinds.Resolve.Should().Be("Asset.Resolve");
        _ = AssetOperationKinds.Browse.Should().Be("Asset.Browse");
        _ = AssetOperationKinds.CopyIdentity.Should().Be("Asset.CopyIdentity");
        _ = ContentBrowserOperationKinds.Navigate.Should().Be("ContentBrowser.Navigate");
        _ = ContentBrowserOperationKinds.Refresh.Should().Be("ContentBrowser.Refresh");
    }

    [TestMethod]
    public void AssetIdentityDiagnosticCodes_ShouldMatchEdM06Vocabulary()
    {
        _ = AssetIdentityDiagnosticCodes.QueryFailed.Should().Be("OXE.ASSETID.QueryFailed");
        _ = AssetIdentityDiagnosticCodes.ReduceFailed.Should().Be("OXE.ASSETID.ReduceFailed");
        _ = AssetIdentityDiagnosticCodes.ResolveMissing.Should().Be("OXE.ASSETID.Resolve.Missing");
        _ = AssetIdentityDiagnosticCodes.DescriptorBroken.Should().Be("OXE.ASSETID.Descriptor.Broken");
        _ = AssetIdentityDiagnosticCodes.CookedMissing.Should().Be("OXE.ASSETID.Cooked.Missing");
        _ = AssetIdentityDiagnosticCodes.ContentRootInvalidSelection.Should().Be("OXE.PROJECT.CONTENT_ROOT.InvalidSelection");
        _ = AssetIdentityDiagnosticCodes.AssetStale.Should().Be("OXE.CONTENTPIPELINE.Asset.Stale");
    }
}
