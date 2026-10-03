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
        var model = new PerspectiveCameraViewModel(fixture.Commands, () => fixture.Context);
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
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        model.Dispose();
        near.Value = original * 3;
        await model.PendingEdits.ConfigureAwait(false);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
    }
}
