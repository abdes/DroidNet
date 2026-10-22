// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.SceneInspector.Components.Transform;
using Oxygen.Editor.SceneInspector.TestSupport;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>
/// The Transform section edits Ignore Parent Transform, a node flag, through the scene document: the value lands in the
/// node's flags, as one undo step.
/// </summary>
[TestClass]
[TestCategory("Inspector")]
internal sealed class TransformNodeFlagsTests : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>Gets or sets the current test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task SettingIgnoreParentTransform_WritesTheNodeFlagsAndIsUndoable() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = InspectorModels.CreateModel("Transform", fixture);
        var transform = (TransformViewModel)model;

        transform.IgnoreParentTransform = true;
        await transform.PendingEdits.ConfigureAwait(true);

        _ = fixture.Node.Flags.IgnoreParentTransform.Should().BeTrue();
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
        await fixture.Document.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Node.Flags.IgnoreParentTransform.Should().BeFalse();
    });
}
