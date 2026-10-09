// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Inspector.Environment;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Inspector;

[TestClass]
public sealed class SkyLightRuntimeStatusTests
{
    [TestMethod]
    [DataRow(RuntimeSkyLightUnavailableReason.MissingCubemap, "No cubemap is selected")]
    [DataRow(RuntimeSkyLightUnavailableReason.ResourceResolveFailed, "failed to load")]
    [DataRow(RuntimeSkyLightUnavailableReason.NotTextureCube, "is not a cubemap")]
    [DataRow(RuntimeSkyLightUnavailableReason.UnsupportedFormat, "float (HDR)")]
    [DataRow(RuntimeSkyLightUnavailableReason.ProcessingFailed, "failed to process")]
    [DataRow(RuntimeSkyLightUnavailableReason.GpuProductsPending, "Loading the cubemap")]
    public void UnavailableSkyLightNamesItsCause(RuntimeSkyLightUnavailableReason reason, string cause)
        => _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { SkyLightUnavailableReason = reason })
            .Should().Contain(cause);

    [TestMethod]
    public void EmptyCaptureAsksForSomethingToCapture()
        => _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { SkyLightUsable = true, SkyLightEmptyCapture = true })
            .Should().StartWith("Nothing to capture");

    [TestMethod]
    public void WorkingDisabledOrUnrenderedSkyLightsSayNothing()
    {
        _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { SkyLightUsable = true }).Should().BeNull();
        _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { SkyLightEnabled = false, SkyLightUnavailableReason = RuntimeSkyLightUnavailableReason.MissingCubemap }).Should().BeNull();
        _ = SkyLightSectionViewModel.DescribeRuntime(Rendered() with { SkyLightObserved = false, SkyLightUnavailableReason = RuntimeSkyLightUnavailableReason.MissingCubemap }).Should().BeNull();
        _ = SkyLightSectionViewModel.DescribeRuntime(state: null).Should().BeNull();
    }

    private static RuntimeEnvironmentState Rendered() => new() { SkyLightObserved = true, SkyLightEnabled = true };
}
