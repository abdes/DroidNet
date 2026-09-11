// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks that composed transform rotations retain the same native orientation after reopening.</summary>
public sealed partial class InspectorControlTests
{
    private static readonly string[] RotationFields = ["RotationX", "RotationY", "RotationZ"];

    /// <summary>Editing all three rotation axes must not produce a different runtime pose after Save/reopen.</summary>
    /// <param name="pitch">The X rotation in degrees.</param>
    /// <param name="yaw">The Y rotation in degrees.</param>
    /// <param name="roll">The Z rotation in degrees.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(20f, 30f, 40f)]
    [DataRow(100f, 10f, 20f)]
    [DataRow(-100f, 30f, -20f)]
    public Task TransformRotationDoesNotChangeWhenTheSavedSceneIsReopened(float pitch, float yaw, float roll) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedNativeNode(scene, "Transform"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        DroidNet.Tests.VisualUserInterfaceTestsApp.MainWindow.Activate();
        var node = fixture.Source.RootNodes.Single();
        using var host = fixture.CreateInspectorHost([node]);
        var model = host.PropertyEditors.OfType<TransformViewModel>().Single();
        var view = new TransformView { ViewModel = model };
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var values = new[] { pitch, yaw, roll };
        for (var axis = 0; axis < RotationFields.Length; axis++)
        {
            var field = NativeNodeFields.Single(value => string.Equals(value.Field, RotationFields[axis], StringComparison.Ordinal));
            var control = await FindInspectorControlAsync(scroller, () => FindNodeControl(view, model, field), field.Field, timeout.Token).ConfigureAwait(true);
            await SetEnvironmentControlValueAsync(control, values[axis]).ConfigureAwait(true);
            while (fixture.Context.History.UndoStack.Count < axis + 1)
            {
                await Task.Delay(10, timeout.Token).ConfigureAwait(true);
            }
        }

        var before = await fixture.ReadNodeAsync(node.Id, timeout.Token).ConfigureAwait(true);
        var sourceRotation = node.Components.OfType<TransformComponent>().Single().LocalRotation;
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        var after = await fixture.ReadNodeAsync(node.Id, timeout.Token).ConfigureAwait(true);
        _ = fixture.Source.RootNodes.Single().Components.OfType<TransformComponent>().Single().LocalRotation.Should().Be(sourceRotation);
        foreach (var expected in before.Properties.Where(value => value.ComponentId == 1 && value.FieldId is >= 3 and <= 5))
        {
            var actual = after.Properties.Single(value => value.ComponentId == expected.ComponentId && value.FieldId == expected.FieldId);
            _ = actual.Value.Should().BeApproximately(expected.Value, 0.001f, "saved reopen must retain native rotation field {0}", expected.FieldId);
        }
    });
}
