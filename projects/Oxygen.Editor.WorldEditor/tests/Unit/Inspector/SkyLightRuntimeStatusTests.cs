// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Engine;
using Oxygen.Editor.World.Inspector.Environment;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Inspector;

[TestClass]
public sealed class SkyLightRuntimeStatusTests
{
    [TestMethod]
    [DataRow(SceneSkyLightUnavailableReason.MissingCubemap, "No cubemap is selected")]
    [DataRow(SceneSkyLightUnavailableReason.ResourceResolveFailed, "failed to load")]
    [DataRow(SceneSkyLightUnavailableReason.NotTextureCube, "is not a cubemap")]
    [DataRow(SceneSkyLightUnavailableReason.UnsupportedFormat, "float (HDR)")]
    [DataRow(SceneSkyLightUnavailableReason.ProcessingFailed, "failed to process")]
    [DataRow(SceneSkyLightUnavailableReason.GpuProductsPending, "Loading the cubemap")]
    public void UnavailableSkyLightNamesItsCause(SceneSkyLightUnavailableReason reason, string cause)
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
        _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { Enabled = false, UnavailableReason = SceneSkyLightUnavailableReason.MissingCubemap }).Should().BeNull();
        _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { Observed = false, UnavailableReason = SceneSkyLightUnavailableReason.MissingCubemap }).Should().BeNull();
        _ = SkyLightSectionViewModel.DescribeRuntime(state: null).Should().BeNull();
    }

    private static SceneSkyLightObservation Rendered() => new(Observed: true, Enabled: true, SceneSkyLightUnavailableReason.None, EmptyCapture: false);
}
