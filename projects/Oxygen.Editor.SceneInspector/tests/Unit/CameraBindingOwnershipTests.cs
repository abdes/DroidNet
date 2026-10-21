// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Inspector;

[TestClass]
public sealed class CameraBindingOwnershipTests
{
    [TestMethod]
    public async Task CameraBinding_RejectsRestoresAndDetachesWithoutParallelScalarState()
    {
        using var fixture = new SceneAuthoringFixture();
        var model = new PerspectiveCameraViewModel(() => fixture.Authoring);
        model.UpdateValues([fixture.Node]);
        var near = model.NearPlane;
        var original = near.Value;
        near.Value = float.NaN;
        await model.PendingEdits.ConfigureAwait(false);
        _ = near.Value.Should().Be(original);
        _ = model.NearPlaneDiagnostic.HasError.Should().BeTrue();
        near.Value = original * 2;
        await model.PendingEdits.ConfigureAwait(false);
        _ = model.NearPlane.Should().BeSameAs(near);
        _ = model.NearPlaneDiagnostic.HasError.Should().BeFalse();
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
        model.Dispose();
        near.Value = original * 3;
        await model.PendingEdits.ConfigureAwait(false);
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task CameraBinding_EditsPhysicalExposureRejectsNonPositiveAndUndoes()
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = new PerspectiveCameraViewModel(() => fixture.Authoring);
        model.UpdateValues([fixture.Node]);
        var originalAperture = fixture.Camera.ApertureF;

        model.ApertureF.Value = 2.8f;
        model.ShutterRate.Value = 60f;
        await model.PendingEdits.ConfigureAwait(false);
        model.Iso.Value = 0f;
        await model.PendingEdits.ConfigureAwait(false);

        _ = fixture.Camera.ApertureF.Should().Be(2.8f);
        _ = fixture.Camera.ShutterRate.Should().Be(60f);
        _ = fixture.Camera.Iso.Should().Be(100f);
        _ = model.IsoDiagnostic.HasError.Should().BeTrue();

        while (fixture.Document.History.UndoStack.Count > 0)
        {
            await fixture.Document.History.UndoAsync(CancellationToken.None).ConfigureAwait(false);
        }

        _ = fixture.Camera.ApertureF.Should().Be(originalAperture);
        _ = fixture.Camera.ShutterRate.Should().Be(125f);
    }
}
