// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.SceneInspector.Environment;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

[TestClass]
public sealed class SkyLightRuntimeStatusTests
{
    [TestMethod]
    [DataRow(SkyLightUnavailableReason.MissingCubemap, "No cubemap is selected")]
    [DataRow(SkyLightUnavailableReason.ResourceResolveFailed, "failed to load")]
    [DataRow(SkyLightUnavailableReason.NotTextureCube, "is not a cubemap")]
    [DataRow(SkyLightUnavailableReason.UnsupportedFormat, "float (HDR)")]
    [DataRow(SkyLightUnavailableReason.ProcessingFailed, "failed to process")]
    [DataRow(SkyLightUnavailableReason.GpuProductsPending, "Loading the cubemap")]
    public void UnavailableSkyLightNamesItsCause(SkyLightUnavailableReason reason, string cause)
        => _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { UnavailableReason = reason })
            .Should().Contain(cause);

    [TestMethod]
    public void EmptyCaptureAsksForSomethingToCapture()
        => _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { EmptyCapture = true })
            .Should().StartWith("Nothing to capture");

    [TestMethod]
    public void WorkingDisabledOrUnrenderedSkyLightsSayNothing()
    {
        _ = SkyLightSectionViewModel.DescribeRuntime(Rendered()).Should().BeNull();
        _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { Enabled = false, UnavailableReason = SkyLightUnavailableReason.MissingCubemap }).Should().BeNull();
        _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { Observed = false, UnavailableReason = SkyLightUnavailableReason.MissingCubemap }).Should().BeNull();
        _ = SkyLightSectionViewModel.DescribeRuntime(state: null).Should().BeNull();
    }

    private static SkyLightObservation Rendered() => new(Observed: true, Enabled: true, SkyLightUnavailableReason.None, EmptyCapture: false);
}
