// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Tests.TestSupport;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

public sealed partial class ScenePreviewTests
{
    [TestMethod]
    public async Task Backdrop_TravelsInOneEnvironmentCommandWithItsCubemapsOnlyWhenUsed()
    {
        using var fixture = new PreviewFixture();
        var (scene, _, _) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Requests.Clear();

        // A solid color backdrop that does not light the scene: the cubemaps behind the unused sources are not loaded.
        var environment = new SceneEnvironmentData
        {
            AtmosphereEnabled = false,
            Background = new() { Enabled = true, ColorRgb = new Vector3(0.25f, 0.5f, 0.75f) },
            SkySphere = new() { Enabled = false, Source = SkySphereSource.SolidColor, Cubemap = new Uri("asset:///Content/Sky.otex.json") },
            SkyLight = new() { Source = SkyLightSource.CapturedScene, Cubemap = new Uri("asset:///Content/Light.otex.json") },
        };
        scene.SetEnvironment(environment);

        var result = await fixture.Preview.UpdateEnvironmentAsync(scene, environment, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Overall.Should().Be(SyncStatus.Accepted);
        _ = result.PerField.Keys.Should().Contain([nameof(SceneEnvironmentData.Background), nameof(SceneEnvironmentData.SkySphere), nameof(SceneEnvironmentData.SkyLight)]);
        var command = fixture.World.Commands.OfType<RuntimeSetEnvironment>().Should().ContainSingle().Which;
        _ = command.Background.Should().Be(environment.Background);
        _ = command.SkySphere.Should().Be(environment.SkySphere);
        _ = command.SkyLight.Should().Be(environment.SkyLight);
        _ = command.SkySphereCubemap.Should().BeNull();
        _ = command.SkyLightCubemap.Should().BeNull();
    }
}
