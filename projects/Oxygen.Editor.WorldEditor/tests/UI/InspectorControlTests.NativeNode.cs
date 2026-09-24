// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises component controls against native state and saved scene reloads.</summary>
public sealed partial class InspectorControlTests
{
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
    [DataRow("Light", "CascadeDistance4")]
    [DataRow("Light", "DistributionExponent")]
    [DataRow("Light", "TransitionFraction")]
    [DataRow("Light", "DistanceFadeoutFraction")]
    public Task NodeFieldControlHistoryAndReopenReachNativeState(string kind, string fieldName) => EnqueueAsync(async () =>
    {
        var field = NativeNodeFields.Single(value => string.Equals(value.Kind, kind, StringComparison.Ordinal) && string.Equals(value.Field, fieldName, StringComparison.Ordinal));
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedNativeNode(scene, kind));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        VisualUserInterfaceTestsApp.MainWindow.Activate();
        var node = fixture.Source.RootNodes.Single();
        using var host = fixture.CreateInspectorHost([node]);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = CreateNumericView(model);
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var control = await FindInspectorControlAsync(scroller, () => FindNodeControl(view, model, field), field.Field, timeout.Token).ConfigureAwait(true);
        var before = SourceNodeProperties(node);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await AssertNodeValuesAsync(fixture, node.Id, before, model, timeout.Token).ConfigureAwait(true);

        await SetEnvironmentControlValueAsync(control, field.ControlValue).ConfigureAwait(true);
        await WaitForNodeControlCommitAsync(fixture, model, timeout.Token).ConfigureAwait(true);
        var expected = new Dictionary<(ushort component, ushort field), float>(before) { [(field.Component, field.NativeField)] = field.ExpectedValue };

        await AssertNodeValuesAsync(fixture, node.Id, expected, model, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, before, model, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, expected, model, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        await AssertNodeValuesAsync(fixture, node.Id, expected, model, timeout.Token).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    private static void SeedNativeNode(Scene scene, string kind)
    {
        var node = new SceneNode(scene) { Name = kind };
        if (string.Equals(kind, "Camera", StringComparison.Ordinal))
        {
            _ = node.AddComponent(new PerspectiveCamera { Name = "Camera" });
        }
        else if (string.Equals(kind, "Light", StringComparison.Ordinal))
        {
            _ = node.AddComponent(new DirectionalLightComponent { Name = "Light", CastsShadows = true });
        }

        scene.RootNodes.Add(node);
    }

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

    private static async Task AssertNodeValuesAsync(
        NativeSceneFixture fixture,
        Guid nodeId,
        Dictionary<(ushort component, ushort field), float> expected,
        IPropertyEditor<SceneNode> model,
        CancellationToken cancellationToken)
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

    private static FrameworkElement? FindNodeControl(UserControl view, IPropertyEditor<SceneNode> model, NodeFieldCase field)
    {
        if (model is TransformViewModel)
        {
            var group = field.Field[..^1];
            return view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>(card => Equals(card.PropertyName, group))?
                .FindDescendant<NumberBox>(number => string.Equals(number.Name, $"PartNumberBox{field.Field[^1]}", StringComparison.Ordinal));
        }

        return model is DirectionalLightViewModel light
            ? field.Field switch
            {
                "AtmosphereSlot" => view.FindDescendant<ComboBox>(combo => ReferenceEquals(combo.ItemsSource, light.AtmosphereSlotOptions)),
                "UsePerPixelAtmosphereTransmittance" => view.FindDescendant<ToggleSwitch>(toggle => Equals(toggle.Header, "Per-pixel transmittance")),
                "AffectsWorld" => view.FindDescendant<ToggleSwitch>(toggle => Equals(toggle.Header, "Affects World")),
                "CastsShadows" => view.FindDescendant<ToggleSwitch>(toggle => Equals(toggle.Header, "Cast")),
                "ContactShadows" => view.FindDescendant<ToggleSwitch>(toggle => Equals(toggle.Header, "Contact")),
                "ShadowResolutionHint" => view.FindDescendant<ComboBox>(combo => ReferenceEquals(combo.ItemsSource, light.ShadowResolutionOptions)),
                "SplitMode" => view.FindDescendant<ComboBox>(combo => ReferenceEquals(combo.ItemsSource, light.SplitModeOptions)),
                _ => view.FindDescendant<NumberBox>(number => Equals(number.Tag, field.Field)),
            }
            : view.FindDescendant<NumberBox>(number => Equals(number.Tag, field.Field));
    }
}
