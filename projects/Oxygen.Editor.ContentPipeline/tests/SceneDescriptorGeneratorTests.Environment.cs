// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Numerics;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks the complete authored post-process contract against native descriptor output.</summary>
public sealed partial class SceneDescriptorGeneratorTests
{
    /// <summary>Schema bounds and coupled exposure fields reject invalid authoring states.</summary>
    [TestMethod]
    public void ValidatePostProcessRejectsMalformedValues()
    {
        PostProcessEnvironmentData[] invalid =
        [
            new() { AutoExposureBlackInfluence = float.NaN },
            new() { AutoExposureBlackInfluence = 1.1f },
            new() { AutoExposureTransitionDistanceEv = 0f },
            new() { AutoExposureTransitionDistanceEv = float.PositiveInfinity },
            new() { AutoExposureMinEv = 17f },
            new() { AutoExposureLowPercentile = 0.9f },
            new() { AutoExposureLogLuminanceRange = float.Epsilon },
            new() { AutoExposureMinLogLuminance = 20f },
            new() { AutoExposureMeteringMode = (MeteringMode)99 },
            new() { ExposureMode = (ExposureMode)99 },
            new() { ToneMapper = (ToneMappingMode)99 },
            new() { ExposureKey = 0f },
            new() { DisplayGamma = 0f },
            new() { AutoExposureCompensationCurve = default },
            new() { AutoExposureCompensationCurve = [new(2f, 1f), new(2f, 2f)] },
            new() { AutoExposureCompensationCurve = [new(0f, float.NaN)] },
            new() { AutoExposureCompensationCurve = Enumerable.Range(0, 65).Select(static i => new ExposureCompensationKeyData(i, 0f)).ToImmutableArray() },
            new() { AutoExposureMeteringMask = new Uri("mask.otex", UriKind.Relative) },
        ];
        foreach (var authored in invalid)
        {
            _ = SceneDescriptorGenerator.ValidatePostProcess(authored).Should().NotBeNullOrEmpty();
        }
    }

    /// <summary>Valid zero controls and EV values outside the old UI clamp survive unchanged.</summary>
    [TestMethod]
    public void ValidatePostProcessAcceptsSupportedBoundaryValues()
    {
        var authored = new PostProcessEnvironmentData
        {
            ManualExposureEv = 30f,
            AutoExposureTargetLuminance = 0f,
            AutoExposureSpeedUp = 0f,
            AutoExposureSpeedDown = 0f,
            AutoExposureBlackInfluence = 1f,
            AutoExposureLowPercentile = 0f,
            AutoExposureHighPercentile = 1f,
            AutoExposureMinLogLuminance = -24f,
            AutoExposureLogLuminanceRange = 56f,
        };
        _ = SceneDescriptorGenerator.ValidatePostProcess(authored).Should().BeNull();
        _ = authored.ManualExposureEv.Should().Be(30f);
    }

    /// <summary>Every authored post-process property and enum combination survives descriptor generation.</summary>
    /// <param name="exposureMode">The authored exposure mode.</param>
    /// <returns>The asynchronous descriptor mapping test.</returns>
    [TestMethod]
    [DataRow(ExposureMode.Manual)]
    [DataRow(ExposureMode.ManualCamera)]
    [DataRow(ExposureMode.Auto)]
    public async Task GenerateAsyncShouldPreserveEveryPostProcessField(ExposureMode exposureMode)
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        scene.RootNodes.Add(new SceneNode(scene) { Name = "Root" });
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        foreach (var toneMapper in Enum.GetValues<ToneMappingMode>())
        {
            foreach (var metering in Enum.GetValues<MeteringMode>())
            {
                var authored = CreatePostProcessSample(exposureMode, toneMapper, metering);
                scene.SetEnvironment(new SceneEnvironmentData
                {
                    AtmosphereEnabled = false,
                    BackgroundColor = new Vector3(0.05f, 0.25f, 0.75f),
                    PostProcess = authored,
                });
                var savedScene = await RoundTripSavedSceneAsync(scene, workspace.Project).ConfigureAwait(false);
                var result = await generator.GenerateAsync(savedScene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);
                _ = result.Diagnostics.Should().BeEmpty();
                using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
                _ = document.RootElement.GetProperty("version").GetInt32().Should().Be(6);
                var environment = document.RootElement.GetProperty("environment");
                _ = environment.GetProperty("sky_atmosphere").GetProperty("enabled").GetBoolean().Should().BeFalse();
                var post = environment.GetProperty("post_process_volume");
                foreach (var property in typeof(PostProcessEnvironmentData).GetProperties())
                {
                    var nativeName = JsonNamingPolicy.SnakeCaseLower.ConvertName(property.Name);
                    _ = post.TryGetProperty(nativeName, out var encoded).Should().BeTrue($"{property.Name} must be represented in the native descriptor");
                    switch (property.GetValue(authored))
                    {
                        case float value: _ = encoded.GetSingle().Should().Be(value, property.Name); break;
                        case bool value: _ = encoded.GetBoolean().Should().Be(value, property.Name); break;
                        case Uri value: _ = encoded.GetString().Should().Be(value.AbsolutePath[..^5], property.Name); break;
                        case ImmutableArray<ExposureCompensationKeyData> keys:
                            _ = encoded.GetArrayLength().Should().Be(keys.Length);
                            for (var index = 0; index < keys.Length; index++)
                            {
                                _ = encoded[index].GetProperty("metered_ev").GetSingle().Should().Be(keys[index].MeteredEv);
                                _ = encoded[index].GetProperty("compensation_ev").GetSingle().Should().Be(keys[index].CompensationEv);
                            }

                            break;
                        case Enum value: _ = encoded.GetInt32().Should().Be(Convert.ToInt32(value, System.Globalization.CultureInfo.InvariantCulture), property.Name); break;
                        default: Assert.Fail($"Add a descriptor check for {property.Name}."); break;
                    }
                }

                var background = environment.GetProperty("background");
                _ = background.GetProperty("enabled").GetBoolean().Should().BeTrue();
                _ = background.GetProperty("color_rgb").EnumerateArray().Select(static value => value.GetSingle()).Should().Equal(0.05f, 0.25f, 0.75f);
            }
        }
    }

    private static async Task<Scene> RoundTripSavedSceneAsync(Scene scene, IProject project)
    {
        var stream = new MemoryStream();
        await using var lifetime = stream.ConfigureAwait(false);
        var serializer = new SceneSerializer(project);
        await serializer.SerializeAsync(stream, scene).ConfigureAwait(false);
        stream.Position = 0;
        return await serializer.DeserializeAsync(stream).ConfigureAwait(false);
    }

    private static PostProcessEnvironmentData CreatePostProcessSample(ExposureMode exposureMode, ToneMappingMode toneMapper, MeteringMode metering)
        => new()
        {
            ToneMapper = toneMapper,
            ExposureMode = exposureMode,
            ExposureEnabled = exposureMode == ExposureMode.ManualCamera,
            ExposureCompensationEv = 1.25f,
            ExposureKey = 8.75f,
            ManualExposureEv = 11.5f,
            AutoExposureMinEv = -3.25f,
            AutoExposureMaxEv = 12.75f,
            AutoExposureSpeedUp = 5.5f,
            AutoExposureSpeedDown = 1.75f,
            AutoExposureMeteringMode = metering,
            AutoExposureLowPercentile = 0.2f,
            AutoExposureHighPercentile = 0.85f,
            AutoExposureMinLogLuminance = -10.5f,
            AutoExposureLogLuminanceRange = 21.25f,
            AutoExposureTargetLuminance = 0.27f,
            AutoExposureSpotMeterRadius = 0.35f,
            AutoExposureBlackInfluence = 0.35f,
            AutoExposureTransitionDistanceEv = 2.5f,
            AutoExposureMeteringMask = new Uri("asset:///Content/Textures/Meter.otex.json"),
            AutoExposureCompensationCurve = [new(-4f, 1f), new(12f, -0.5f)],
            BloomIntensity = 0.6f,
            BloomThreshold = 2.25f,
            Saturation = 0.8f,
            Contrast = 1.4f,
            VignetteIntensity = 0.3f,
            DisplayGamma = 2.4f,
        };
}
