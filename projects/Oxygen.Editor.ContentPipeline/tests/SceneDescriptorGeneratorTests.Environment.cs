// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks the complete authored post-process contract against native descriptor output.</summary>
public sealed partial class SceneDescriptorGeneratorTests
{
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
                _ = document.RootElement.GetProperty("version").GetInt32().Should().Be(4);
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
            BloomIntensity = 0.6f,
            BloomThreshold = 2.25f,
            Saturation = 0.8f,
            Contrast = 1.4f,
            VignetteIntensity = 0.3f,
            DisplayGamma = 2.4f,
        };
}
