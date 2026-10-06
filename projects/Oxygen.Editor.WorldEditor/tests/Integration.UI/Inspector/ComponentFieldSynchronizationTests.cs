// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldCases;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldControls;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Inspector;

[TestClass]
public sealed partial class ComponentFieldSynchronizationTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Each component field preserves source and native values through history and Save/reopen.</summary>
    /// <param name="kind">The inspector component.</param>
    /// <param name="fieldName">The numeric, boolean or enum field.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Transform", "PositionX")]
    [DataRow("Transform", "PositionY")]
    [DataRow("Transform", "PositionZ")]
    [DataRow("Transform", "RotationX")]
    [DataRow("Transform", "RotationY")]
    [DataRow("Transform", "RotationZ")]
    [DataRow("Transform", "ScaleX")]
    [DataRow("Transform", "ScaleY")]
    [DataRow("Transform", "ScaleZ")]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Camera", "AspectRatio")]
    [DataRow("Camera", "NearPlane")]
    [DataRow("Camera", "FarPlane")]
    [DataRow("Light", "ColorR")]
    [DataRow("Light", "ColorG")]
    [DataRow("Light", "ColorB")]
    [DataRow("Light", "AffectsWorld")]
    [DataRow("Light", "DiskScaleR")]
    [DataRow("Light", "DiskScaleG")]
    [DataRow("Light", "DiskScaleB")]
    [DataRow("Light", "CastsShadows")]
    [DataRow("Light", "ShadowBias")]
    [DataRow("Light", "ShadowNormalBias")]
    [DataRow("Light", "ContactShadows")]
    [DataRow("Light", "ShadowResolutionHint")]
    [DataRow("Light", "ExposureCompensation")]
    [DataRow("Light", "IntensityLux")]
    [DataRow("Light", "AngularSizeRadians")]
    [DataRow("Light", "UsePerPixelAtmosphereTransmittance")]
    [DataRow("Light", "AtmosphereSlot")]
    [DataRow("Light", "CascadeCount")]
    [DataRow("Light", "SplitMode")]
    [DataRow("Light", "MaxShadowDistance")]
    [DataRow("Light", "CascadeDistance1")]
    [DataRow("Light", "CascadeDistance2")]
    [DataRow("Light", "CascadeDistance3")]
    [DataRow("Light", "DistributionExponent")]
    [DataRow("Light", "TransitionFraction")]
    [DataRow("Light", "DistanceFadeoutFraction")]
    public Task NodeFieldControlHistoryAndReopenReachNativeState(string kind, string fieldName) => EnqueueAsync(async () =>
    {
        var field = NativeNodeFields.Single(value => string.Equals(value.Kind, kind, StringComparison.Ordinal) && string.Equals(value.Field, fieldName, StringComparison.Ordinal));
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedNativeNode(scene, kind, field.Arrange));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var node = fixture.Source.RootNodes.Single();
        using var host = fixture.CreateInspectorHost([node]);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = CreateNumericView(model);
        var scroller = new ScrollViewer
        {
            Content = view,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto
        };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var control = await FindNodeFieldControlAsync(view, scroller, model, field, timeout.Token).ConfigureAwait(true);
        var before = SourceNodeProperties(node);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await AssertNodeValuesAsync(fixture, node.Id, before, model, timeout.Token).ConfigureAwait(true);
        await SetEnvironmentControlValueAsync(control, field.ControlValue).ConfigureAwait(true);
        await WaitForNodeControlCommitAsync(fixture, model, timeout.Token).ConfigureAwait(true);
        var expected = new Dictionary<(ushort component, ushort field), float>(before)
        {
            [(field.Component, field.NativeField)] = field.ExpectedValue
        };
        await AssertNodeValuesAsync(fixture, node.Id, expected, model, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, before, model, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, expected, model, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, expected, model, timeout.Token).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>The fourth cascade boundary is displayed disabled because a fifth cascade would be required to edit it, and its stored value survives Save/reopen unchanged.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task CascadeDistance4IsDisplayedDisabledAndItsStoredBoundarySurvivesReopen() => EnqueueAsync(async () =>
    {
        var field = NativeNodeFields.Single(value => string.Equals(value.Kind, "Light", StringComparison.Ordinal) && string.Equals(value.Field, "CascadeDistance4", StringComparison.Ordinal));
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedNativeNode(scene, "Light", field.Arrange));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var node = fixture.Source.RootNodes.Single();
        using var host = fixture.CreateInspectorHost([node]);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, "Light"));
        var view = CreateNumericView(model);
        var scroller = new ScrollViewer
        {
            Content = view,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto
        };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var control = await FindRealizedNodeFieldControlAsync(view, model, field, timeout.Token).ConfigureAwait(true);
        _ = control.Should().BeAssignableTo<NumberBox>("the stored boundary is displayed through the same field control as editable boundaries");
        var box = (NumberBox)control;
        _ = box.IsLoaded.Should().BeTrue("showing non-editable data is intended and hides nothing");
        _ = box.IsEnabled.Should().BeFalse("the boundary is editable only once a fifth cascade exists, which cascade_count validates out of range");
        _ = box.NumberValue.Should().BeApproximately(field.ExpectedValue, 0.0001f, "the disabled box shows the stored boundary");
        _ = model.Should().BeAssignableTo<DirectionalLightViewModel>();
        _ = ((DirectionalLightViewModel)model).IsCascadeDistance4Applicable.Should().BeFalse("a manual split with the maximal cascade count still leaves the fourth boundary inapplicable");
        var expected = SourceNodeProperties(node);
        _ = expected[(field.Component, field.NativeField)].Should().BeApproximately(field.ExpectedValue, 0.0001f);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await AssertNodeValuesAsync(fixture, node.Id, expected, model, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, expected, model, timeout.Token).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    private static async Task WaitForNodeControlCommitAsync(NativeSceneFixture fixture, IPropertyEditor<SceneNode> model, CancellationToken cancellationToken)
    {
        if (model is not TransformViewModel)
        {
            await PendingNumericEdits(model).ConfigureAwait(true);
        }

        for (var attempt = 0; attempt < 100 && fixture.Context.History.UndoStack.Count == 0; attempt++)
        {
            await Task.Delay(10, cancellationToken).ConfigureAwait(true);
        }

        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
    }

    private static async Task AssertNodeValuesAsync(NativeSceneFixture fixture, Guid nodeId, Dictionary<(ushort component, ushort field), float> expected, IPropertyEditor<SceneNode> model, CancellationToken cancellationToken)
    {
        var source = SourceNodeProperties(fixture.Source.RootNodes.Single(node => node.Id == nodeId));
        var native = await fixture.ReadNodeAsync(nodeId, cancellationToken).ConfigureAwait(true);
        _ = native.Exists.Should().BeTrue();
        _ = native.Properties.Should().HaveCount(expected.Count);
        foreach (var (key, value) in expected)
        {
            _ = source[key].Should().BeApproximately(value, 0.0001f, "source component {0}, field {1}", key.component, key.field);
            var actual = native.Properties.Single(property => property.ComponentId == key.component && property.FieldId == key.field);
            _ = actual.Value.Should().BeApproximately(value, 0.0001f, "native component {0}, field {1}", key.component, key.field);
        }

        foreach (var property in model.GetType().GetProperties().Where(property => property.PropertyType == typeof(InspectorFieldDiagnostic)))
        {
            _ = ((InspectorFieldDiagnostic)property.GetValue(model)!).Message.Should().BeEmpty("valid input must have no {0} error", property.Name);
        }
    }

    private static async Task<FrameworkElement> FindRealizedNodeFieldControlAsync(UserControl view, IPropertyEditor<SceneNode> model, NodeFieldCase field, CancellationToken cancellationToken)
    {
        for (var step = 0; step <= 200; step++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            foreach (var disclosure in view.FindDescendants().OfType<Expander>())
            {
                disclosure.IsExpanded = true;
            }

            if (FindNodeControl(view, model, field) is { IsLoaded: true } control)
            {
                return control;
            }

            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
            {
            }).ConfigureAwait(true);
        }

        throw new InvalidOperationException($"Node field {field.Field} was never realized after expanding every disclosure.");
    }
}
