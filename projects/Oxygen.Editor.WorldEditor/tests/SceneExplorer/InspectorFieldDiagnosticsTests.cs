// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

[TestClass]
public sealed class InspectorFieldDiagnosticsTests
{
    [TestMethod]
    public void NewerDependentEditClearsItsErrorWithoutClearingUnrelatedFields()
    {
        var near = new PropertyId("camera", "/near");
        var far = new PropertyId("camera", "/far");
        var intensity = new PropertyId("light", "/intensity");
        var sut = new InspectorFieldDiagnostics();
        sut.Relate(near, far);
        var first = sut.Begin([near], revision: 7);
        var other = sut.Begin([intensity], revision: 7);
        var rejected = new SceneCommandResult(Succeeded: false) { ValidationCode = "INVALID", ValidationMessage = "Invalid value" };
        sut.Complete(first, rejected);
        sut.Complete(other, rejected);

        var newer = sut.Begin([far], revision: 8);
        sut.Complete(newer, SceneCommandResult.Success);
        sut.Complete(first, rejected);

        _ = sut.Get(near).Message.Should().BeEmpty();
        _ = sut.Get(far).Message.Should().BeEmpty();
        _ = sut.Get(intensity).Message.Should().Be("Invalid value");
        _ = sut.Get(far).Revision.Should().Be(8);
    }

    [TestMethod]
    public void RebindingDiscardsAnEarlierDocumentCompletion()
    {
        var field = new PropertyId("camera", "/near");
        var sut = new InspectorFieldDiagnostics();
        var pending = sut.Begin([field], revision: 3);
        sut.Reset();

        sut.Complete(pending, new SceneCommandResult(Succeeded: false) { ValidationMessage = "Old document error" });

        _ = sut.Get(field).Message.Should().BeEmpty();
    }
}
