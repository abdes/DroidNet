// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Environment;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorModels;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "partial class with public part")]
public sealed partial class InspectorBindingTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The transform inspector realizes its three rows, each with its unit and its accessible name.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task TransformInspectorXamlRealizesItsNumericControls() => AssertInspectorRealizesAsync(
        static fixture => CreateModel("Transform", fixture),
        static model => new TransformView { ViewModel = (TransformViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: static view =>
        {
            var rows = view.FindDescendants().OfType<Oxygen.Editor.Controls.PropertyCard>().ToArray();
            _ = rows.Select(row => (row.PropertyName, row.Qualifier, row.QualifierGroup)).Should().Equal(
                ("Position", "m", "Transform"),
                ("Rotation", "°", "Transform"),
                ("Scale", "x", "Transform"));
            _ = rows.Select(row => AutomationProperties.GetName((VectorBox)row.Content)).Should().Equal("Position, metres", "Rotation, degrees", "Scale, multiplier");
            return Task.CompletedTask;
        });

    /// <summary>The camera inspector realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task CameraInspectorXamlRealizesItsNumericControls() => AssertInspectorRealizesAsync(
        static fixture => CreateModel("Camera", fixture),
        static model => new PerspectiveCameraView { ViewModel = (PerspectiveCameraViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: null);

    /// <summary>The light inspector realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task LightInspectorXamlRealizesItsNumericControls() => AssertInspectorRealizesAsync(
        static fixture => CreateModel("Light", fixture),
        static model => new DirectionalLightView { ViewModel = (DirectionalLightViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: null);

    /// <summary>The orthographic camera inspector realizes its size and framing controls.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task OrthographicCameraInspectorXamlRealizesItsControls() => AssertInspectorRealizesAsync(
        static fixture => CreateModel("OrthographicCamera", fixture),
        static model => new OrthographicCameraView { ViewModel = (OrthographicCameraViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: static view =>
        {
            _ = view.FindDescendants().OfType<NumberBox>().Should().Contain(number => Equals(number.Tag, "OrthographicSize"));
            _ = view.FindDescendants().OfType<ComboBox>().Should().ContainSingle().Which.SelectedItem.Should().Be(Oxygen.Managed.Core.CameraAspectMode.Auto);
            return Task.CompletedTask;
        });

    /// <summary>The point light inspector realizes its emission and shadow controls.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task PointLightInspectorXamlRealizesItsControls() => AssertInspectorRealizesAsync(
        static fixture => CreateModel("PointLight", fixture),
        static model => new PointLightView { ViewModel = (PointLightViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: static view =>
        {
            _ = view.FindDescendants().OfType<NumberBox>().Select(number => number.Tag).Should().Contain(["LuminousFluxLumens", "Range", "SourceRadius"]);
            return Task.CompletedTask;
        });

    /// <summary>The spot light inspector realizes its cone controls in display degrees.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task SpotLightInspectorXamlRealizesItsConeControlsInDegrees() => AssertInspectorRealizesAsync(
        static fixture => CreateModel("SpotLight", fixture),
        static model => new SpotLightView { ViewModel = (SpotLightViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: static view =>
        {
            var section = view.FindDescendant<Oxygen.Editor.Controls.PropertiesExpander>()!;
            var outer = section.Items.OfType<Oxygen.Editor.Controls.InspectorNumberField>().Single(field => Equals(field.Tag, "OuterConeAngleRadians"));
            _ = outer.NumberValue.Should().BeApproximately(0.5f * 180f / MathF.PI, 0.01f);
            return Task.CompletedTask;
        });

    /// <summary>For geometry, the node Rendering section realizes all three authored flag toggles.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task NodeRenderingXamlRealizesItsFlagToggles() => AssertInspectorRealizesAsync(
        static fixture => CreateModel("Rendering", fixture),
        static model => new NodeRenderingView { ViewModel = (NodeRenderingViewModel)model },
        prepareFixture: static fixture => fixture.Node.Components.Add(new GeometryComponent { Name = "Geometry" }),
        assertRealizedAsync: static view =>
        {
            _ = view.FindDescendants().OfType<ToggleSwitch>().Should().HaveCount(3);
            return Task.CompletedTask;
        });

    /// <summary>For a light without geometry, the Rendering section realizes only Scene Visibility.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task NodeRenderingXamlHidesShadowFlagsWithoutGeometry() => AssertInspectorRealizesAsync(
        static fixture => CreateModel("Rendering", fixture),
        static model => new NodeRenderingView { ViewModel = (NodeRenderingViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: static view =>
        {
            _ = view.FindDescendants().OfType<ToggleSwitch>().Should().ContainSingle()
                .Which.GetValue(AutomationProperties.NameProperty).Should().Be("Scene visibility");
            return Task.CompletedTask;
        });

    /// <summary>The atmosphere-lights inspector realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task AtmosphereLightsXamlRealizesItsNumericControls() => AssertInspectorRealizesAsync(
        static fixture => CreateModel("Environment", fixture),
        static model => new AtmosphereLightsSectionView { ViewModel = ((EnvironmentViewModel)model).AtmosphereLights },
        static fixture => fixture.Node.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary,
        async view =>
        {
            var section = view.FindDescendant<Oxygen.Editor.Controls.PropertiesExpander>()!;
            var sources = section.Items.OfType<Oxygen.Editor.Controls.PropertyCard>().Single();
            _ = section.BringItemIntoView(sources).Should().BeTrue();
            await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var primary = sources.FindDescendants().OfType<Expander>().First();
            primary.IsExpanded = true;
            await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = view.FindDescendants().OfType<NumberBox>().Should().Contain(number => Equals(number.Tag, "SunAzimuth"));
        });

    /// <summary>The material editor realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task MaterialEditorXamlRealizesItsNumericControls() => AssertInspectorRealizesAsync(
        static fixture => CreateModel("Material", fixture),
        static model => new MaterialEditorView { ViewModel = (MaterialEditorViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: async view =>
        {
            var scroller = view.FindDescendant<ScrollViewer>()!;
            _ = await FindInspectorControlAsync(scroller, () => view.FindDescendant<NumberBox>(number => Equals(number.Tag, "RoughnessFactor")), "Material.RoughnessFactor", this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = view.KeyboardAccelerators.Should().OnlyContain(accelerator => ReferenceEquals(accelerator.ScopeOwner, view));
        });

    /// <summary>The atmosphere source disclosures start collapsed.</summary>
    /// <returns>The source-disclosure task.</returns>
    [TestMethod]
    public Task AtmosphereSourceDisclosuresStartCollapsed() => EnqueueAsync(async () =>
    {
        var (fixture, model, view) = await LoadAtmosphereLightsSectionAsync(LoadTestContentAsync).ConfigureAwait(true);
        var atmosphereSection = (Oxygen.Editor.Controls.PropertiesExpander)view.FindName("AtmosphereLightsSection");
        var sourceCard = atmosphereSection.Items.OfType<Oxygen.Editor.Controls.PropertyCard>().Single();
        _ = atmosphereSection.BringItemIntoView(sourceCard).Should().BeTrue();
        await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var sourceDisclosures = sourceCard.FindDescendants().OfType<Expander>().ToArray();
        _ = sourceDisclosures.Should().HaveCount(2);
        var primary = sourceDisclosures[0];
        var secondary = sourceDisclosures[1];
        _ = primary.IsExpanded.Should().BeFalse();
        _ = secondary.IsExpanded.Should().BeFalse();
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>The sky section retains the sun disk switch and its four design field labels.</summary>
    /// <returns>The sky-control regression task.</returns>
    [TestMethod]
    public Task SkySectionRetainsSunDiskAndDesignFieldLabels() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        var sky = scenario.Section("SkyAtmosphereSection");
        var sunDisk = SceneCards(sky).Single(card => string.Equals(card.PropertyName, "Sun Disk", StringComparison.Ordinal));
        _ = await scenario.BringIntoViewAsync(sky, sunDisk).ConfigureAwait(true);
        _ = sunDisk.FindDescendant<ToggleSwitch>(control => Equals(control.Tag, "SunDiskEnabled")).Should().NotBeNull();
        _ = SceneFieldLabels(sky).Should().Contain(["Distance scale", "Scattering strength", "Start distance", "Height fog contribution"]);
        _ = sky.Items.OfType<Expander>().Should().ContainSingle(group => Equals(group.Header, "Planet & ground"));
        scenario.AssertCleanHistory();
    });

    /// <summary>The ground albedo swatch edits linear RGB from the display picker and the picked edit undoes.</summary>
    /// <returns>The albedo editing regression task.</returns>
    [TestMethod]
    public Task GroundAlbedoSwatchEditsLinearRgbAndUndoes() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        var sky = scenario.Section("SkyAtmosphereSection");
        await scenario.ExpandGroupAsync("SkyAtmosphereSection", "Planet & ground").ConfigureAwait(true);
        await scenario.SearchAsync("Ground Albedo").ConfigureAwait(true);
        var planet = sky.Items.OfType<Expander>().Single(group => Equals(group.Header, "Planet & ground"));
        _ = await scenario.BringIntoViewAsync(sky, planet).ConfigureAwait(true);
        var albedoField = (Oxygen.Editor.Controls.InspectorColorField)await FindInspectorControlAsync(
            (ScrollViewer)scenario.View.FindName("ScenePropertyScroll"),
            () => scenario.View.FindDescendant<Oxygen.Editor.Controls.InspectorColorField>(field =>
                field.IsLoaded && Equals(field.Tag, "GroundAlbedo") && SwatchOf(field) is not null),
            "GroundAlbedo",
            this.TestContext.CancellationToken).ConfigureAwait(true);
        var swatch = SwatchOf(albedoField);
        _ = swatch.Flyout.Should().BeOfType<Flyout>();
        scenario.Model.SkyAtmosphere.GroundAlbedoR = AlbedoRedLinear;
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = SwatchColor(albedoField).R.Should().Be(128);
        var historyCount = scenario.Fixture.Document.History.UndoStack.Count;
        await PickDisplayColorAsync(swatch, Windows.UI.Color.FromArgb(255, 128, 64, 32)).ConfigureAwait(true);
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = scenario.Fixture.Scene.Environment.SkyAtmosphere.GroundAlbedoRgb.Y.Should().BeApproximately(AlbedoGreenLinearPicked, 0.000001f);
        _ = scenario.Fixture.Document.History.UndoStack.Should().HaveCount(historyCount + 1);
        await scenario.Fixture.Document.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = SwatchColor(albedoField).R.Should().Be(128);
    });

    /// <summary>Scene search reveals the inactive auto exposure minimum with its applicability note.</summary>
    /// <returns>The search-reveal regression task.</returns>
    [TestMethod]
    public Task SceneSearchRevealsInactiveAutoFieldWithApplicabilityNote() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, width: null, height: null, host: ScenarioHost.Scroll).ConfigureAwait(true);
        var exposure = scenario.Section("ExposureSection");
        exposure.IsExpanded = false;
        var autoMinimum = scenario.Element<Oxygen.Editor.Controls.InspectorNumberField>("AutoExposureMinEvCard");

        // precondition (was asserted mid-test): manual exposure keeps the auto minimum inactive
        _ = scenario.Model.Exposure.ExposureMode.Should().Be(Oxygen.Editor.World.Serialization.ExposureMode.Manual);
        _ = scenario.Named("AutoExposureMinEvCard").Visibility.Should().Be(Visibility.Collapsed);

        await scenario.SearchAsync("auto_exposure_min_ev").ConfigureAwait(true);
        _ = scenario.Named("AutoExposureMinEvCard").Visibility.Should().Be(Visibility.Visible);
        _ = autoMinimum.ApplicabilityText.Should().Contain(AppliesInAutoExposureModeCopy);
        _ = exposure.IsExpanded.Should().BeTrue();
    });

    /// <summary>The scope selector filters sections by browsing scope and the no-match indicator tracks query hits.</summary>
    /// <returns>The scope-filtering regression task.</returns>
    [TestMethod]
    public Task SceneScopeSelectorFiltersSectionsAndShowsNoMatchIndicator() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, width: null, height: null, host: ScenarioHost.Scroll).ConfigureAwait(true);
        var exposure = scenario.Section("ExposureSection");
        var sky = scenario.Section("SkyAtmosphereSection");
        var background = scenario.Section("BackdropSection");
        var references = (FrameworkElement)scenario.View.FindName("SceneReferencesView");

        // collapsed baseline: search restore cycles must return the section to this state
        exposure.IsExpanded = false;

        await scenario.SelectScopeAsync("Post-processing").ConfigureAwait(true);
        _ = sky.Visibility.Should().Be(Visibility.Collapsed);
        _ = background.Visibility.Should().Be(Visibility.Collapsed);
        _ = exposure.Visibility.Should().Be(Visibility.Visible);
        _ = references.Visibility.Should().Be(Visibility.Collapsed);

        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        await scenario.SelectScopeAsync("References").ConfigureAwait(true);
        _ = references.Visibility.Should().Be(Visibility.Visible);
        _ = sky.Visibility.Should().Be(Visibility.Collapsed);

        await scenario.SearchAsync("script").ConfigureAwait(true);
        _ = references.Visibility.Should().Be(Visibility.Visible);
        _ = ((TextBlock)scenario.View.FindName("NoScenePropertyMatches")).Visibility.Should().Be(Visibility.Collapsed);
        await scenario.SearchAsync("no-such-reference").ConfigureAwait(true);
        _ = references.Visibility.Should().Be(Visibility.Collapsed);
        _ = ((TextBlock)scenario.View.FindName("NoScenePropertyMatches")).Visibility.Should().Be(Visibility.Visible);

        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        await scenario.SelectScopeAsync("Post-processing").ConfigureAwait(true);
        await scenario.SearchAsync("manual").ConfigureAwait(true);
        _ = scenario.Named("AutoExposureMinEvCard").Visibility.Should().Be(Visibility.Collapsed);
        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        _ = exposure.IsExpanded.Should().BeFalse();
        _ = background.Visibility.Should().Be(Visibility.Collapsed);

        await scenario.SelectScopeAsync("All").ConfigureAwait(true);
        await scenario.SearchAsync("background").ConfigureAwait(true);
        _ = background.Visibility.Should().Be(Visibility.Visible);
        _ = sky.Visibility.Should().Be(Visibility.Collapsed);
        _ = exposure.Visibility.Should().Be(Visibility.Collapsed);
        await scenario.SearchAsync("no-such-property").ConfigureAwait(true);
        _ = background.Visibility.Should().Be(Visibility.Collapsed);
        _ = ((TextBlock)scenario.View.FindName("NoScenePropertyMatches")).Visibility.Should().Be(Visibility.Visible);
        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        _ = background.Visibility.Should().Be(Visibility.Visible);
    });

    /// <summary>The display gamma card stays visible and labeled when tone mapping is set to None.</summary>
    /// <returns>The tone-mapping applicability regression task.</returns>
    [TestMethod]
    public Task DisplayGammaCardAppearsWhenToneMappingIsNone() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        scenario.Model.PostProcessing.ToneMapping = Oxygen.Editor.World.Serialization.ToneMappingMode.None;
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = scenario.Named("DisplayGammaCard").Visibility.Should().Be(Visibility.Visible);
        _ = scenario.Element<Oxygen.Editor.Controls.InspectorNumberField>("DisplayGammaCard").Label.Should().Be("Display Gamma");
    });

    /// <summary>Scene extra-asset references are added and removed through the interactive inspector.</summary>
    /// <returns>The UI authoring regression task.</returns>
    [TestMethod]
    public Task SceneExtraAssetReferencesCanBeAddedAndRemovedInteractively() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, host: ScenarioHost.Scroll).ConfigureAwait(true);

        var referencesView = (SceneReferencesSectionView)scenario.View.FindName("SceneReferencesView");
        var section = referencesView.FindDescendant<Oxygen.Editor.Controls.PropertiesExpander>()!;
        var extraAssetCard = section.Items.OfType<Oxygen.Editor.Controls.PropertyCard>()
            .Single(card => string.Equals(card.PropertyName, "Extra AssetValue", StringComparison.Ordinal));
        _ = section.BringItemIntoView(extraAssetCard).Should().BeTrue();

        const string path = "/Content/Materials/Shared.omat";
        var pathInput = extraAssetCard.FindDescendants().OfType<TextBox>().Single();
        pathInput.Text = path;
        await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        // Bounded realization wait: a single render pass can miss the button while the repeater materializes it (recorded flake).
        var add = await WaitForDescendantAsync(section, (Button button) => Equals(button.Content, "Add Extra AssetValue"), "add-extra-asset button", this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = add.IsEnabled.Should().BeTrue();
        ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(add)
            .GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = scenario.Fixture.Scene.References.ExtraAssets.Should().ContainSingle().Which.Should().Be(path);
        _ = scenario.Fixture.Document.IsDirty.Should().BeTrue();

        var remove = await WaitForDescendantAsync(section, (Button button) => Equals(button.Content, "Remove"), "remove-extra-asset button", this.TestContext.CancellationToken).ConfigureAwait(true);
        ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(remove)
            .GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = scenario.Fixture.Scene.References.ExtraAssets.Should().BeEmpty();
        _ = scenario.Fixture.Document.History.UndoStack.Should().HaveCount(2);
    });

    /// <summary>The design-order sections, Play first, keep every scene card and closed secondary groups.</summary>
    /// <returns>The design-inventory regression task.</returns>
    [TestMethod]
    public Task SceneSectionsFollowDesignInventory() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var sections = ((StackPanel)scenario.View.FindName("SceneSections")).Children.SelectMany(child => child switch
        {
            UserControl { Content: Oxygen.Editor.Controls.PropertiesExpander section } => [section],
            UserControl { Content: StackPanel effects } => effects.Children.OfType<Oxygen.Editor.Controls.PropertiesExpander>(),
            _ => Enumerable.Empty<Oxygen.Editor.Controls.PropertiesExpander>(),
        }).ToArray();
        _ = sections.Select(section => section.Header).Should().Equal("Play", "Scene References", "Atmosphere Lights", "Backdrop", "Sky Atmosphere", "Sky Light", "Fog", "Exposure", "Tone Mapping", "Color Grading", "Bloom");
        _ = sections.SelectMany(SceneCards).Should().HaveCount(97);
        _ = sections.Select(section => section.IsExpanded).Should().Equal(true, true, true, true, true, true, true, true, true, false, false);
        var sky = sections.Single(section => Equals(section.Header, "Sky Atmosphere"));
        var skyGroups = sky.Items.OfType<Expander>().ToArray();
        _ = skyGroups.Select(group => group.Header).Should().Equal("Planet & ground", "Scattering", "Aerial perspective");
        _ = skyGroups.Select(group => group.IsExpanded).Should().OnlyContain(expanded => !expanded);
        _ = skyGroups[0].IsExpanded.Should().BeFalse();
        AssertDisclosureCardOrder(skyGroups[0], "PlanetRadiusKm", "AtmosphereHeightKm", "GroundAlbedo");
        AssertDisclosureCardOrder(skyGroups[1], "RayleighScaleHeightKm", "MieScaleHeightKm", "MieAnisotropy");
        AssertDisclosureCardOrder(skyGroups[2], "AerialPerspectiveDistanceScale", "AerialScatteringStrength", "AerialPerspectiveStartDepthMeters", "HeightFogContribution");

        var exposure = sections.Single(section => Equals(section.Header, "Exposure"));
        var exposureGroups = exposure.Items.OfType<Expander>().ToArray();
        _ = exposureGroups.Select(group => group.Header).Should().Equal("Metering & limits", "Adaptation", "Histogram & calibration", "Exposure shaping");
        _ = exposureGroups.Select(group => group.IsExpanded).Should().Equal(true, false, false, false);
        AssertDisclosureCardOrder(exposureGroups[0], "AutoExposureMeteringMode", "AutoExposureMinEv", "AutoExposureMaxEv", "AutoExposureTargetLuminance", "AutoExposureSpotMeterRadius");
        AssertDisclosureCardOrder(exposureGroups[1], "AutoExposureSpeedUp", "AutoExposureSpeedDown", "AutoExposureTransitionDistanceEv");
        AssertDisclosureCardOrder(exposureGroups[2], "ExposureKey", "AutoExposureLowPercentile", "AutoExposureHighPercentile", "AutoExposureMinLogLuminance", "AutoExposureLogLuminanceRange", "AutoExposureBlackInfluence");
        AssertDisclosureCardOrder(exposureGroups[3], "AutoExposureMeteringMask", "AutoExposureCompensationCurve");
        scenario.AssertCleanHistory();
    });

    /// <summary>Scene search expands the nested planet disclosure to reveal its card and restores it collapsed when cleared.</summary>
    /// <returns>The nested-disclosure restore regression task.</returns>
    [TestMethod]
    public Task SceneSearchExpandsNestedDisclosureAndRestoresCollapsedState() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var sky = scenario.Section("SkyAtmosphereSection");
        var planet = sky.Items.OfType<Expander>().Single(group => Equals(group.Header, "Planet & ground"));
        await scenario.SearchAsync("ground_albedo").ConfigureAwait(true);
        _ = planet.IsExpanded.Should().BeTrue();
        _ = scenario.Named("GroundAlbedoCard").Visibility.Should().Be(Visibility.Visible);
        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        _ = planet.IsExpanded.Should().BeFalse();
        scenario.AssertCleanHistory();
    });

    /// <summary>Applicability changes rewrite only the field's note: its editor and the stored value survive.</summary>
    /// <returns>The applicability regression task.</returns>
    [TestMethod]
    public Task SceneSearchKeepsFieldContentAndStoredValuesWhileApplicabilityChanges() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, host: ScenarioHost.None).ConfigureAwait(true);
        var exposure = scenario.Section("ExposureSection");
        var spot = scenario.Element<Oxygen.Editor.Controls.InspectorNumberField>("AutoExposureSpotMeterRadiusCard");
        var originalContent = CardOf(spot).Content;
        var storedRadius = scenario.Fixture.Scene.Environment.PostProcess.AutoExposureSpotMeterRadius;
        await scenario.SearchAsync("spot radius").ConfigureAwait(true);
        _ = scenario.Named("AutoExposureSpotMeterRadiusCard").Visibility.Should().Be(Visibility.Visible);
        _ = spot.ApplicabilityText.Should().Contain(AppliesInAutoExposureSpotMeteringCopy);
        _ = CardOf(spot).Content.Should().BeSameAs(originalContent);
        _ = scenario.Fixture.Document.IsDirty.Should().BeFalse();
        _ = scenario.Fixture.Document.History.UndoStack.Should().BeEmpty();

        scenario.Model.Exposure.ExposureMode = Oxygen.Editor.World.Serialization.ExposureMode.Auto;
        scenario.Model.Exposure.AutoExposureMeteringMode = Oxygen.Editor.World.Serialization.MeteringMode.Spot;
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = spot.ApplicabilityText.Should().NotStartWith(StoredValueNotePrefixCopy);
        await scenario.SearchAsync("spot radius missing-field").ConfigureAwait(true);
        _ = ((TextBlock)scenario.View.FindName("NoScenePropertyMatches")).Visibility.Should().Be(Visibility.Visible);
        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        _ = CardOf(spot).Content.Should().BeSameAs(originalContent);
        _ = scenario.Fixture.Scene.Environment.PostProcess.AutoExposureSpotMeterRadius.Should().Be(storedRadius);
    });

    /// <summary>Swapping the view onto a replacement model detaches the previous one: later edits to it cannot move the view.</summary>
    /// <returns>The observer-ownership regression task.</returns>
    [TestMethod]
    public Task SceneSearchObservesReplacementModelWithoutRetainingThePreviousObserver() => EnqueueAsync(async () =>
    {
        using var first = new SceneAuthoringFixture();
        using var second = new SceneAuthoringFixture();
        using var firstModel = (EnvironmentViewModel)CreateModel("Environment", first);
        using var secondModel = (EnvironmentViewModel)CreateModel("Environment", second);
        var view = new EnvironmentView { ViewModel = firstModel, Width = 420, Height = 780 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        view.ViewModel = secondModel;
        await WaitForRenderAsync().ConfigureAwait(true);
        var search = (TextBox)view.FindName("ScenePropertySearchBox");
        search.Text = "auto_exposure_min_ev";
        await WaitForRenderAsync().ConfigureAwait(true);
        var field = (Oxygen.Editor.Controls.InspectorNumberField)FindInspectorElement(view, "AutoExposureMinEvCard");

        // The replacement model is still Manual: the stored-value note names that mode explicitly,
        // so a stale observer applying the first model's switch to Auto would visibly rewrite it.
        _ = field.ApplicabilityText.Should().StartWith(StoredValueNotePrefixCopy).And.Contain("Current mode: Manual");
        string[] ObservedState() => [field.Visibility.ToString(), field.ApplicabilityText];
        var observedBefore = ObservedState();

        // A stale observer on the previous model would push this applicability change into the view.
        firstModel.Exposure.ExposureMode = Oxygen.Editor.World.Serialization.ExposureMode.Auto;
        await firstModel.PendingEdits.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = firstModel.Exposure.ExposureMode.Should().Be(Oxygen.Editor.World.Serialization.ExposureMode.Auto);
        _ = view.ViewModel.Should().BeSameAs(secondModel);
        _ = ObservedState().Should().Equal(observedBefore);
        _ = second.Scene.Environment.PostProcess.AutoExposureMinEv.Should().Be(-6);
    });

    /// <summary>The realized near-plane editor rejects an invalid commit and displays current inline feedback.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task CameraTextCommitRejectsInvalidNearPlaneAndClearsItsDiagnosticOnCorrection() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (PerspectiveCameraViewModel)CreateModel("Camera", fixture);
        var view = new PerspectiveCameraView
        {
            ViewModel = model,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, "NearPlane"))!;
        var field = number.FindAscendant<Oxygen.Editor.Controls.InspectorNumberField>()!;
        var before = fixture.Camera.NearPlane;

        // Invalid by the fixture contract: twice the far plane can never be a valid near plane.
        var invalidNearPlane = (fixture.Camera.FarPlane * 2f).ToString(CultureInfo.InvariantCulture);
        await EnterTextAsync(number, invalidNearPlane).ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.NearPlane.Should().Be(before);
        _ = number.NumberValue.Should().Be(before);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        _ = model.NearPlaneDiagnostic.Message.Should().NotBeEmpty();
        _ = field.HasError.Should().BeTrue();
        _ = field.ErrorText.Should().Be(model.NearPlaneDiagnostic.Message);
        await EnterTextAsync(number, "2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.NearPlane.Should().Be(2);
        _ = model.NearPlaneDiagnostic.Message.Should().BeEmpty();
        _ = field.HasError.Should().BeFalse();
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
    });

    /// <summary>A zero scale axis shows scoped inline feedback without changing the model or history.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task TransformTextValidationPreservesScaleUntilAValidCommit() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (TransformViewModel)CreateModel("Transform", fixture);
        var view = new TransformView
        {
            ViewModel = model,
        };
        var scroller = new ScrollViewer
        {
            Content = view,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var card = view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>(element => string.Equals(element.PropertyName, "Scale", StringComparison.Ordinal))!;
        card.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        var number = card.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        await EnterTextAsync(number, "0").ConfigureAwait(true);

        // Live validation: the diagnostic surfaces while typing, before any commit.
        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalScale.X.Should().Be(1);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        _ = model.ScaleXDiagnostic.Message.Should().NotBeEmpty();
        _ = VisibleFeedback(view).Should().ContainSingle().Which.Should().Be("X: " + model.ScaleXDiagnostic.Message);
        number.CompletePendingTextEdit();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);

        // Commit parity with the camera inspector: committing the rejected value changes nothing.
        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalScale.X.Should().Be(1);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        _ = model.ScaleXDiagnostic.Message.Should().NotBeEmpty();
        await EnterTextAsync(number, "2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalScale.X.Should().Be(2);
        _ = model.ScaleXDiagnostic.Message.Should().BeEmpty();
        _ = VisibleFeedback(view).Should().BeEmpty();
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
    });

    /// <summary>Vector child-control events traverse the view and group a hundred samples into one entry.</summary>
    /// <param name="field">The environment vector control.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("GroundAlbedo")]
    [DataRow("SkyLuminance")]
    [DataRow("SolidColor")]
    public Task EnvironmentVectorControlGroupsSamplesAndUndoRefreshesTheControl(string field) => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        if (VectorFieldPreparations.TryGetValue(field, out var prepare))
        {
            await prepare(scenario).ConfigureAwait(true);
        }

        var propertyScroll = (ScrollViewer)scenario.View.FindName("ScenePropertyScroll");
        var vector = await FindVisibleVectorAsync(scenario.View, propertyScroll, field).ConfigureAwait(true);
        var number = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        var before = number.NumberValue;
        number.OnEditSessionStarted(NumberBoxEditInteractionKind.PointerDrag);
        for (var sample = 1; sample <= 100; sample++)
        {
            number.NumberValue = sample / 200f;
        }

        number.OnEditSessionCompleted(NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = scenario.Fixture.Document.History.UndoStack.Should().ContainSingle();
        _ = number.NumberValue.Should().Be(0.5f);
        await scenario.Fixture.Document.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before);
    });

    private static async Task<(SceneAuthoringFixture fixture, EnvironmentViewModel model, AtmosphereLightsSectionView view)> LoadAtmosphereLightsSectionAsync(
        Func<FrameworkElement, Task> loadContent,
        double width = 420)
    {
        var fixture = new SceneAuthoringFixture();
        fixture.Node.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary;
        var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new AtmosphereLightsSectionView { ViewModel = model.AtmosphereLights };
        var host = new Grid { Width = width, Height = 780, RequestedTheme = ElementTheme.Dark, Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Windows.UI.Color.FromArgb(255, 37, 40, 43)) };
        host.Resources.MergedDictionaries.Add(new ResourceDictionary { Source = new Uri("ms-appx:///Microsoft.UI.Xaml/DensityStyles/Compact.xaml") });
        host.Children.Add(view);
        await loadContent(host).ConfigureAwait(true);
        return (fixture, model, view);
    }

    // The axis errors the Transform rows announce.
    private static string[] VisibleFeedback(TransformView view)
        => [.. view.FindDescendants().OfType<TextBlock>()
            .Where(text => text.Visibility == Visibility.Visible && AutomationProperties.GetLiveSetting(text) == Microsoft.UI.Xaml.Automation.Peers.AutomationLiveSetting.Polite)
            .Select(text => string.Concat(text.Inlines.OfType<Microsoft.UI.Xaml.Documents.Run>().Select(run => run.Text)))];

    private static async Task AssertInspectorRealizesAsync(
        Func<SceneAuthoringFixture, IDisposable> createModel,
        Func<object, UserControl> createView,
        Action<SceneAuthoringFixture>? prepareFixture,
        Func<UserControl, Task>? assertRealizedAsync)
    {
        await EnqueueAsync(async () =>
        {
            using var fixture = new SceneAuthoringFixture();
            prepareFixture?.Invoke(fixture);
            using var model = createModel(fixture);
            var view = createView(model);
            await LoadTestContentAsync(view).ConfigureAwait(true);
            if (assertRealizedAsync is null)
            {
                _ = view.FindDescendant<NumberBox>().Should().NotBeNull();
                return;
            }

            await assertRealizedAsync(view).ConfigureAwait(true);
        }).ConfigureAwait(true);
    }
}
