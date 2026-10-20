// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Numerics;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The value and cross-field rules of the scene's environment, through <see cref="SetSceneProperties"/>.
/// </summary>
/// <remarks>
/// <para>
/// Most tests run on the sample's environment, with two changes where a boundary row would otherwise equal the
/// default: the fog's maximum opacity is 0.5 and its start distance 50, and the sky light's lower hemisphere blend is
/// 0.5. A value a descriptor rejects is <see cref="SceneEditCodes.PropertyOutOfRange"/> naming the property; a rule
/// across fields is <see cref="SceneEditCodes.PropertiesInconsistent"/> naming no node (the environment has none) and
/// no property (scene-edits.md 3.8).
/// </para>
/// </remarks>
[TestClass]
public sealed class SceneEnvironmentRulesTests
{
    public static IEnumerable<TestDataRow<PropertyCase>> InRange =>
    [
        PropertyCase.OfScene("PlanetRadius_One", Environment.PlanetRadiusMeters, 1f),
        PropertyCase.OfScene("GroundAlbedo_Zero", Environment.GroundAlbedo, Vector3.Zero),
        PropertyCase.OfScene("GroundAlbedo_One", Environment.GroundAlbedo, Vector3.One),
        PropertyCase.OfScene("MieAnisotropy_Min", Environment.MieAnisotropy, -0.999f),
        PropertyCase.OfScene("MieAnisotropy_Max", Environment.MieAnisotropy, 0.999f),

        // The schema's minimum for the aerial perspective start depth is 0.
        PropertyCase.OfScene("AerialStart_SchemaMinimum", Environment.AerialPerspectiveStartDepthMeters, 0f),
        PropertyCase.OfScene("BlackInfluence_One", Environment.AutoExposureBlackInfluence, 1f),
        PropertyCase.OfScene("FogMaxOpacity_One", SceneFogFields.MaxOpacity, 1f),
        PropertyCase.OfScene("FogDirectionalExponent_Min", SceneFogFields.DirectionalInscatteringExponent, 1e-6f),
        PropertyCase.OfScene("FogDirectionalExponent_Max", SceneFogFields.DirectionalInscatteringExponent, 1000f),
        PropertyCase.OfScene("FogEnd_BelowStart", SceneFogFields.EndDistanceMeters, 10f),
        PropertyCase.OfScene("SkyLightLowerHemisphereBlend_One", SceneSkyFields.SkyLightLowerHemisphereBlendAlpha, 1f),
    ];

    public static IEnumerable<TestDataRow<PropertyCase>> OutOfRange =>
    [
        PropertyCase.OfScene("ExposureMode_Undefined", Environment.ExposureMode, (ExposureMode)99),
        PropertyCase.OfScene("ToneMapping_Undefined", Environment.ToneMapping, (ToneMappingMode)99),
        PropertyCase.OfScene("ManualExposure_NaN", Environment.ManualExposureEv, float.NaN),
        PropertyCase.OfScene("ExposureCompensation_Infinity", Environment.ExposureCompensation, float.PositiveInfinity),
        PropertyCase.OfScene("PlanetRadius_BelowOne", Environment.PlanetRadiusMeters, 0.99f),
        PropertyCase.OfScene("AtmosphereHeight_BelowOne", Environment.AtmosphereHeightMeters, 0.99f),
        PropertyCase.OfScene("GroundAlbedo_AboveOne", Environment.GroundAlbedo, new Vector3(0.4f, 1.01f, 0.4f)),
        PropertyCase.OfScene("RayleighHeight_BelowOne", Environment.RayleighScaleHeightMeters, 0.99f),
        PropertyCase.OfScene("MieAnisotropy_One", Environment.MieAnisotropy, 1f),
        PropertyCase.OfScene("SkyLuminance_Negative", Environment.SkyLuminance, new Vector3(1, -0.01f, 1)),
        PropertyCase.OfScene("AerialDistanceScale_Negative", Environment.AerialPerspectiveDistanceScale, -0.01f),
        PropertyCase.OfScene("AerialStart_BelowSchemaMinimum", Environment.AerialPerspectiveStartDepthMeters, -0.01f),
        PropertyCase.OfScene("BlackInfluence_AboveOne", Environment.AutoExposureBlackInfluence, 1.01f),
        PropertyCase.OfScene("TransitionDistance_Zero", Environment.AutoExposureTransitionDistanceEv, 0f),
        PropertyCase.OfScene("Backdrop_None", SceneSkyFields.Backdrop, EnvironmentBackdrop.None),
        PropertyCase.OfScene("FogDensity_Negative", SceneFogFields.Density, -0.01f),
        PropertyCase.OfScene("FogMaxOpacity_AboveOne", SceneFogFields.MaxOpacity, 1.01f),
        PropertyCase.OfScene("FogDirectionalExponent_Zero", SceneFogFields.DirectionalInscatteringExponent, 0f),
        PropertyCase.OfScene("FogVolumetricAlbedo_AboveOne", SceneFogFields.VolumetricAlbedoRgb, new Vector3(1, 1.01f, 1)),
        PropertyCase.OfScene("SkyIntensity_Negative", SceneSkyFields.SkySphereIntensity, -0.01f),
        PropertyCase.OfScene("SkyLightLowerHemisphereBlend_AboveOne", SceneSkyFields.SkyLightLowerHemisphereBlendAlpha, 1.01f),
    ];

    private static SceneEnvironmentDescriptors Environment => SceneProperties.SceneEnvironment;

    [TestMethod]
    [DynamicData(nameof(InRange))]
    public async Task Value_InRange_Applies(PropertyCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SceneWith(DefaultsWithRoomToMove())).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(row.Edit()).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.PropertiesChanged(Node: null, [row.Property], EditPhase.Commit));
    }

    [TestMethod]
    [DynamicData(nameof(OutOfRange))]
    public async Task Value_OutOfRange_IsInvalid(PropertyCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SceneWith(DefaultsWithRoomToMove())).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(row.Edit()).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyOutOfRange, node: null, row.Property);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DataRow(65, false, SceneEditCodes.PropertyOutOfRange, true, DisplayName = "SixtyFiveKeys")]
    [DataRow(2, true, SceneEditCodes.PropertyOutOfRange, true, DisplayName = "RepeatedEv")]
    public async Task CompensationCurve(int keyCount, bool repeatLastEv, string code, bool namesProperty)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SceneWith(DefaultsWithRoomToMove())).ConfigureAwait(false);
        var keys = Enumerable.Range(0, keyCount)
            .Select(index => new ExposureCompensationKeyData(repeatLastEv && index == keyCount - 1 ? index - 1 : index, 0.5f))
            .ToImmutableArray();
        var edit = new SetSceneProperties(new PropertyEdit.Builder().Set(Environment.AutoExposureCompensationCurve, keys).Build(), "Curve");
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Invalid(result, code, node: null, namesProperty ? Environment.AutoExposureCompensationCurve.Id : null);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task BackgroundColorAboveOne_WhileBackgroundShown_IsInconsistent()
    {
        var environment = new SceneEnvironmentData
        {
            AtmosphereEnabled = false,
            Background = new BackgroundEnvironmentData { Enabled = true, ColorRgb = new Vector3(0.2f, 0.2f, 0.2f) },
        };
        using var fixture = await AuthoringFixture.CreateAsync(SceneWith(environment)).ConfigureAwait(false);
        var edit = new SetSceneProperties(new PropertyEdit.Builder().Set(SceneSkyFields.SolidColor, new Vector3(2, 0, 0)).Build(), "Background");
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertiesInconsistent);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DataRow("PostProcess")]
    [DataRow("SkyAtmosphere")]
    [DataRow("Fog")]
    [DataRow("SkySphere")]
    [DataRow("SkyLight")]
    public async Task GroupProperty_Undo_RestoresTheWholeGroup(string group)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SceneWith(DefaultsWithRoomToMove())).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder();
        _ = group switch
        {
            "PostProcess" => edit.Set(Environment.ExposureCompensation, 1.5f),
            "SkyAtmosphere" => edit.Set(Environment.PlanetRadiusMeters, 1_000f),
            "Fog" => edit.Set(SceneFogFields.Density, 0.5f),
            "SkySphere" => edit.Set(SceneSkyFields.SkySphereIntensity, 2f),
            _ => edit.Set(SceneSkyFields.SkyLightIntensity, 2f),
        };

        _ = await SceneAssert.RoundTripAsync(fixture, new SetSceneProperties(edit.Build(), group)).ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(EnvironmentBackdrop.Atmosphere, EnvironmentBackdrop.Cubemap, DisplayName = "AtmosphereToCubemap")]
    [DataRow(EnvironmentBackdrop.Atmosphere, EnvironmentBackdrop.SolidColor, DisplayName = "AtmosphereToSolidColor")]
    [DataRow(EnvironmentBackdrop.Cubemap, EnvironmentBackdrop.Atmosphere, DisplayName = "CubemapToAtmosphere")]
    [DataRow(EnvironmentBackdrop.Cubemap, EnvironmentBackdrop.SolidColor, DisplayName = "CubemapToSolidColor")]
    [DataRow(EnvironmentBackdrop.SolidColor, EnvironmentBackdrop.Atmosphere, DisplayName = "SolidColorToAtmosphere")]
    [DataRow(EnvironmentBackdrop.SolidColor, EnvironmentBackdrop.Cubemap, DisplayName = "SolidColorToCubemap")]
    public async Task Backdrop_Change_UndoRestoresTheCoupledFlags(EnvironmentBackdrop from, EnvironmentBackdrop to)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SceneWith(BackdropEnvironment(from))).ConfigureAwait(false);
        var edit = new SetSceneProperties(new PropertyEdit.Builder().Set(SceneSkyFields.Backdrop, to).Build(), "Backdrop");

        _ = await SceneAssert.RoundTripAsync(fixture, edit).ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task SolidColorLightsScene_Toggle_UndoRestoresWhereTheColorLives(bool lightsScene)
    {
        // The lighting color is HDR (2, 0.5, 0.25); the background color is a display color, so moving the color
        // between them clamps it, and undo must bring back the original place and value.
        var environment = lightsScene
            ? new SceneEnvironmentData
            {
                AtmosphereEnabled = false,
                SkySphere = new SkySphereEnvironmentData { Enabled = true, Source = SkySphereSource.SolidColor, SolidColorRgb = new Vector3(2, 0.5f, 0.25f) },
            }
            : new SceneEnvironmentData
            {
                AtmosphereEnabled = false,
                Background = new BackgroundEnvironmentData { Enabled = true, ColorRgb = new Vector3(0.2f, 0.4f, 0.6f) },
            };
        using var fixture = await AuthoringFixture.CreateAsync(SceneWith(environment)).ConfigureAwait(false);
        var edit = new SetSceneProperties(new PropertyEdit.Builder().Set(SceneSkyFields.SolidColorLightsScene, !lightsScene).Build(), "Solid color");

        _ = await SceneAssert.RoundTripAsync(fixture, edit).ConfigureAwait(false);
    }

    private static SceneEnvironmentData DefaultsWithRoomToMove()
        => new()
        {
            Fog = new FogEnvironmentData { MaxOpacity = 0.5f, StartDistanceMeters = 50f },
            SkyLight = new SkyLightEnvironmentData { LowerHemisphereBlendAlpha = 0.5f },
        };

    private static SceneEnvironmentData BackdropEnvironment(EnvironmentBackdrop backdrop)
        => backdrop switch
        {
            EnvironmentBackdrop.Atmosphere => new SceneEnvironmentData(),
            EnvironmentBackdrop.Cubemap => new SceneEnvironmentData
            {
                AtmosphereEnabled = false,
                SkySphere = new SkySphereEnvironmentData { Enabled = true, Source = SkySphereSource.Cubemap },
            },
            _ => new SceneEnvironmentData
            {
                AtmosphereEnabled = false,
                SkySphere = new SkySphereEnvironmentData { Enabled = true, Source = SkySphereSource.SolidColor, SolidColorRgb = new Vector3(0.3f, 0.3f, 0.3f) },
            },
        };

    private static SceneFileData SceneWith(SceneEnvironmentData environment)
        => SampleScene.Builder().Environment(environment).Build();
}
