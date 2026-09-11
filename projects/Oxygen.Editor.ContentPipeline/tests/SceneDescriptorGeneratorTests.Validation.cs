// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks shared schema bounds and actionable preflight targets.</summary>
public sealed partial class SceneDescriptorGeneratorTests
{
    /// <summary>Rejects invalid saved Aerial Start values with a stable property navigation target.</summary>
    /// <param name="value">The saved invalid value.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow(-1f)]
    [DataRow(float.NaN)]
    [DataRow(float.PositiveInfinity)]
    public async Task InvalidAerialStartNamesItsSceneAndProperty(float value)
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        scene.SetEnvironment(scene.Environment with { SkyAtmosphere = new SkyAtmosphereEnvironmentData { AerialPerspectiveStartDepthMeters = value } });
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        var issue = result.Diagnostics.Single();
        _ = issue.Severity.Should().Be(DiagnosticSeverity.Error);
        _ = issue.AffectedEntity!.SceneId.Should().Be(scene.Id);
        _ = issue.AffectedVirtualPath.Should().Be(scope.Inputs[0].AssetUri.AbsolutePath);
        _ = issue.SuggestedAction!.Payload["PropertyPath"].Should().Be(SceneEnvironmentConstraints.AerialStartPropertyPath);
        _ = issue.SuggestedAction.Payload["AssetUri"].Should().Be(scope.Inputs[0].AssetUri.AbsoluteUri);
        _ = scene.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters.Should().Be(value);
        _ = File.Exists(result.DescriptorPath).Should().BeFalse();
    }

    /// <summary>The schema accepts its lower bound and the user's repaired value.</summary>
    /// <param name="value">The valid distance in meters.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow(0f)]
    [DataRow(100f)]
    public async Task ValidAerialStartSurvivesDescriptorGeneration(float value)
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        scene.RootNodes.Add(new SceneNode(scene) { Name = "Node" });
        scene.SetEnvironment(scene.Environment with { SkyAtmosphere = new SkyAtmosphereEnvironmentData { AerialPerspectiveStartDepthMeters = value } });
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Diagnostics.Should().NotContain(static issue => issue.Severity == DiagnosticSeverity.Error);
        using var descriptor = System.Text.Json.JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = descriptor.RootElement.GetProperty("environment").GetProperty("sky_atmosphere").GetProperty("aerial_perspective_start_depth_m").GetSingle().Should().Be(value);
    }
}
