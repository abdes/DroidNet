// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Inspector;

[TestClass]
public sealed class InspectorFieldDiagnosticsTests
{
    [TestMethod]
    public void ErrorStateIsUiIndependentAndNotifiesWhenTheMessageChanges()
    {
        var diagnostic = new InspectorFieldDiagnostic();
        var notifications = new List<string?>();
        diagnostic.PropertyChanged += (_, args) => notifications.Add(args.PropertyName);
        _ = diagnostic.HasError.Should().BeFalse();

        diagnostic.Message = "Invalid value";
        _ = diagnostic.HasError.Should().BeTrue();
        diagnostic.Message = string.Empty;

        _ = diagnostic.HasError.Should().BeFalse();
        _ = notifications.Where(name => string.Equals(name, nameof(InspectorFieldDiagnostic.HasError), StringComparison.Ordinal)).Should().HaveCount(2);
    }

    [TestMethod]
    public void NewerDependentEditClearsItsErrorWithoutClearingUnrelatedFields()
    {
        var near = new PropertyId("camera", "/near");
        var far = new PropertyId("camera", "/far");
        var intensity = new PropertyId("light", "/intensity");
        var sut = new InspectorFieldDiagnostics();
        sut.Relate(near, far);
        var first = sut.Begin([near]);
        var other = sut.Begin([intensity]);
        var rejected = new SceneEditResult(SceneEditOutcome.Invalid, [SceneDiagnostic.Error("INVALID", "Invalid value")]);
        sut.Complete(first, rejected);
        sut.Complete(other, rejected);

        var newer = sut.Begin([far]);
        sut.Complete(newer, new SceneEditResult(SceneEditOutcome.Applied, []));
        sut.Complete(first, rejected);

        _ = sut.Get(near).Code.Should().BeEmpty();
        _ = sut.Get(far).Code.Should().BeEmpty();
        _ = sut.Get(intensity).Code.Should().Be("INVALID");
    }

    [TestMethod]
    public void RebindingDiscardsAnEarlierDocumentCompletion()
    {
        var field = new PropertyId("camera", "/near");
        var sut = new InspectorFieldDiagnostics();
        var pending = sut.Begin([field]);
        sut.Reset();

        sut.Complete(pending, new SceneEditResult(SceneEditOutcome.Invalid, [SceneDiagnostic.Error("OLD", "Old document error")]));

        _ = sut.Get(field).Code.Should().BeEmpty();
    }
}
