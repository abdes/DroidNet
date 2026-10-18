// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Serialization;

/// <summary>
/// Verifies serialization, deserialization, and migration of scene environment settings.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SceneEnvironmentData))]
public sealed class SceneEnvironmentTests
{
    [TestMethod]
    public async Task Deserialize_MissingEnvironmentField_LoadsDefaults()
    {
        const string json = """
            {
              "Id": "11111111-1111-1111-1111-111111111111",
              "Name": "Default Scene",
              "RootNodes": []
            }
            """;

        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json));
        await using var streamLifetime = stream.ConfigureAwait(false);

        var scene = await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = scene.Environment.Should().Be(new SceneEnvironmentData());
    }

    [TestMethod]
    public async Task Deserialize_SceneSavedWithoutFog_LoadsDefaultFog()
    {
        const string json = """
            {
              "Id": "11111111-1111-1111-1111-111111111111",
              "Name": "Before Fog",
              "RootNodes": [],
              "Environment": { "AtmosphereEnabled": true }
            }
            """;
        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json));
        await using var streamLifetime = stream.ConfigureAwait(false);

        var scene = await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = scene.Environment.Fog.Should().Be(new FogEnvironmentData());
    }

    [TestMethod]
    public async Task Deserialize_SceneWithLegacyBackgroundColor_MigratesToEnabledBackground()
    {
        const string json = """
            {
              "Id": "11111111-1111-1111-1111-111111111111",
              "Name": "Before Backdrop",
              "RootNodes": [],
              "Environment": { "AtmosphereEnabled": false, "BackgroundColor": { "X": 0.1, "Y": 0.2, "Z": 0.3 } }
            }
            """;
        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json));
        await using var streamLifetime = stream.ConfigureAwait(false);

        var scene = await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = scene.Environment.Background.Should().Be(new BackgroundEnvironmentData { Enabled = true, ColorRgb = new Vector3(0.1f, 0.2f, 0.3f) });
        _ = scene.Environment.LegacyBackgroundColor.Should().BeNull();
        _ = scene.Environment.SkySphere.Should().Be(new SkySphereEnvironmentData());
        _ = scene.Environment.SkyLight.Should().Be(new SkyLightEnvironmentData());
        var saved = new MemoryStream();
        await using var savedLifetime = saved.ConfigureAwait(false);
        await SceneSerializer.SerializeAsync(saved, scene).ConfigureAwait(false);
        _ = Encoding.UTF8.GetString(saved.ToArray()).Should().NotContain("BackgroundColor");
    }

    [TestMethod]
    public async Task Deserialize_LegacyBackgroundColorOutsideTheSdrRange_ClampsIt()
    {
        const string json = """
            {
              "Id": "22222222-2222-2222-2222-222222222222",
              "Name": "Bright Background",
              "RootNodes": [],
              "Environment": { "AtmosphereEnabled": false, "BackgroundColor": { "X": 2, "Y": -0.5, "Z": 0.25 } }
            }
            """;
        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json));
        await using var streamLifetime = stream.ConfigureAwait(false);

        var scene = await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = scene.Environment.Background.ColorRgb.Should().Be(new Vector3(1f, 0f, 0.25f));
    }

    [TestMethod]
    public async Task Fog_RoundTripsThroughSaveAndLoad()
    {
        var fog = new FogEnvironmentData
        {
            Enabled = true,
            Density = 0.03f,
            SecondDensity = 0.01f,
            InscatteringLuminanceRgb = new Vector3(0.2f, 0.3f, 0.4f),
            VolumetricFogEnabled = true,
            VolumetricAlbedoRgb = new Vector3(0.5f, 0.6f, 0.7f),
            VisibleInReflectionCaptures = false,
        };
        var scene = Scene.CreateAndHydrate(new SceneData
        {
            Id = SceneId.New(),
            Name = "Fog Round Trip",
            Environment = new SceneEnvironmentData { Fog = fog },
        });

        var restored = await SceneTestSupport.RoundTripAsync(scene).ConfigureAwait(false);

        _ = restored.Environment.Fog.Should().Be(fog);
    }

    [TestMethod]
    public async Task Deserialize_ObsoleteFlatExposure_IsRejected()
    {
        const string json = """
            {
              "Id": "11111111-1111-1111-1111-111111111111",
              "Name": "Obsolete Exposure",
              "RootNodes": [],
              "Environment": { "ExposureMode": "Auto", "ManualExposureEv": 9.7 }
            }
            """;
        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json));
        await using var streamLifetime = stream.ConfigureAwait(false);
        var deserialize = async () => await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);
        _ = await deserialize.Should().ThrowAsync<JsonException>().ConfigureAwait(false);
    }

    [TestMethod]
    public void Hydrate_PreservesAuthoredExposureWithoutDefaultRewrite()
    {
        var exposure = new PostProcessEnvironmentData
        {
            ExposureMode = ExposureMode.Auto,
            ExposureKey = 10.0f,
            ManualExposureEv = 9.7f,
        };
        var scene = Scene.CreateAndHydrate(new SceneData
        {
            Id = SceneId.New(),
            Name = "Authored Exposure",
            Environment = new SceneEnvironmentData { PostProcess = exposure },
        });
        _ = scene.Environment.PostProcess.Should().Be(exposure);
    }

    [TestMethod]
    public async Task Deserialize_NullSkyAtmosphere_LoadsDefaults()
    {
        const string json = """
            {
              "Id": "11111111-1111-1111-1111-111111111111",
              "Name": "Scene With Null Sky",
              "RootNodes": [],
              "Environment": {
                "AtmosphereEnabled": true,
                "SkyAtmosphere": null
              }
            }
            """;

        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json));
        await using var streamLifetime = stream.ConfigureAwait(false);

        var scene = await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = scene.Environment.SkyAtmosphere.Should().Be(new SkyAtmosphereEnvironmentData());
    }

    [TestMethod]
    public async Task Environment_AllFields_RoundTripThroughJson()
    {
        var scene = new Scene { Name = "Environment Scene" };
        var expected = CreateAuthoredEnvironment();
        scene.SetEnvironment(expected);
        expected = scene.Environment;

        var restored = await SceneTestSupport.RoundTripAsync(scene).ConfigureAwait(false);

        _ = restored.Environment.Should().BeEquivalentTo(expected);
    }

    [TestMethod]
    public void Environment_ObsoleteSunNodeIdIsRejected()
    {
        Action read = () => JsonSerializer.Deserialize<SceneEnvironmentData>(
            "{\"SunNodeId\":\"33333333-3333-3333-3333-333333333333\"}");
        _ = read.Should().Throw<JsonException>();
    }

    private static SceneEnvironmentData CreateAuthoredEnvironment() => new()
    {
        AtmosphereEnabled = false,
        Background = new BackgroundEnvironmentData { Enabled = true, ColorRgb = new Vector3(0.2f, 0.3f, 0.4f) },
        SkySphere = new SkySphereEnvironmentData
        {
            Enabled = true,
            Source = SkySphereSource.SolidColor,
            Cubemap = AssetPath.Parse("/Content/Sky.otex.json"),
            SolidColorRgb = new Vector3(2.0f, 3.0f, 4.0f),
            Intensity = 256.0f,
            RotationRadians = 1.5f,
            TintRgb = new Vector3(0.9f, 0.8f, 0.7f),
        },
        SkyLight = new SkyLightEnvironmentData
        {
            Enabled = false,
            Source = SkyLightSource.SpecifiedCubemap,
            Cubemap = AssetPath.Parse("/Content/Light.otex.json"),
            Intensity = 2.0f,
            TintRgb = new Vector3(0.5f, 0.6f, 0.7f),
            DiffuseIntensity = 0.25f,
            SpecularIntensity = 0.75f,
            CubemapAngleRadians = -0.5f,
            LowerHemisphereColor = new Vector3(0.1f, 0.2f, 0.3f),
            LowerHemisphereIsSolidColor = false,
            LowerHemisphereBlendAlpha = 0.4f,
            VolumetricScatteringIntensity = 3.0f,
            AffectReflections = false,
        },
        SkyAtmosphere = new SkyAtmosphereEnvironmentData
        {
            PlanetRadiusMeters = 6_400_000.0f,
            AtmosphereHeightMeters = 90_000.0f,
            GroundAlbedoRgb = new Vector3(0.1f, 0.2f, 0.3f),
            RayleighScaleHeightMeters = 7_000.0f,
            MieScaleHeightMeters = 1_400.0f,
            MieAnisotropy = 0.7f,
            SkyLuminanceFactorRgb = new Vector3(1.1f, 1.0f, 0.9f),
            AerialPerspectiveDistanceScale = 1.2f,
            AerialScatteringStrength = 0.8f,
        },
    };
}
