// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.TestSupport;

internal static class CatalogWorkload
{
    private static readonly string[] EngineShapeNames = ["Cube", "SubdividedCube", "Sphere", "IcoSphere", "Capsule", "Plane", "Cylinder", "Cone", "Torus", "Quad"];

    internal static AssetPath[] EngineGeometries { get; } = [.. EngineShapeNames.Select(EngineAssets.BasicShape)];

    internal static async Task SeedCatalogWorkloadAsync(NativeSceneFixture fixture, ProjectContext project, ContentPipelineService pipeline, CancellationToken cancellationToken)
    {
        var request = new SceneImportRequest(project, Path.Combine(AppContext.BaseDirectory, "Fixtures/static_scalar_triangle.gltf"), "Triangle", AssetPath.Parse("/Content/Models"));
        var imported = await pipeline.ImportSourceAsync(request, cancellationToken).ConfigureAwait(true);
        _ = imported.IsPublished.Should().BeTrue(DescribeWorkloadCook(imported));
        var mesh = imported.CookedAssets.Single(static asset => asset.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        for (var index = 0; index < 998; index++)
        {
            var relative = WorkloadMaterialPath(index);
            var path = fixture.ProjectRoot.Combine(relative);
            _ = Directory.CreateDirectory(path.Parent!.FullPath);
            var name = "M" + index.ToString("D4", CultureInfo.InvariantCulture);
            var source = $$"""
                    {
                      "name": "{{name}}",
                      "parameters": {
                        "base_color": [
                          0.6,
                          0.3,
                          0.15,
                          1
                        ],
                        "metalness": 0,
                        "roughness": 0.5
                      }
                    }
                    """;
            await File.WriteAllTextAsync(path.FullPath, source, cancellationToken).ConfigureAwait(true);
        }

        var scene = fixture.Source;
        var inventories = new Dictionary<AssetPath, GeometrySurfaceMetadata>();
        foreach (var shape in EngineGeometries)
        {
            var shapeSurfaces = await ((IGeometrySurfaceProvider)pipeline).ReadAsync(project, shape, cancellationToken).ConfigureAwait(true);
            _ = shapeSurfaces.Should().NotBeNull();
            inventories.Add(shape, shapeSurfaces!);
        }

        var importedSurfaces = await ((IGeometrySurfaceProvider)pipeline).ReadAsync(project, mesh, cancellationToken).ConfigureAwait(true);
        _ = importedSurfaces.Should().NotBeNull();
        inventories.Add(mesh, importedSurfaces!);
        SeedCatalogWorkloadScene(scene, mesh, inventories);
        await fixture.SaveSceneChangedInMemoryAsync(cancellationToken).ConfigureAwait(true);
        var cooked = await pipeline.CookProjectAsync(cancellationToken).ConfigureAwait(true);
        _ = cooked.IsPublished.Should().BeTrue(DescribeWorkloadCook(cooked));
    }

    internal static void SeedCatalogWorkloadScene(Scene scene, AssetPath mesh, IReadOnlyDictionary<AssetPath, GeometrySurfaceMetadata> inventories)
    {
        for (var index = 0; index < 98; index++)
        {
            var node = new SceneNode(scene, "Geometry " + index.ToString(CultureInfo.InvariantCulture));
            node.Flags.CastsShadows = true;
            node.Flags.ReceivesShadows = true;
            var geometryUri = index >= 90 ? mesh : EngineGeometries[index % EngineGeometries.Length];
            var geometry = new GeometryComponent
            {
                Name = "Geometry",
                Geometry = geometryUri,
            };
            var inventory = inventories[geometryUri];
            var target = new SurfaceTarget(geometryUri, inventory.Surfaces.Should().ContainSingle().Which.SurfaceId, inventory.LayoutRevision);
            geometry.OverrideSlots.Add(new MaterialsSlot { Target = target, Material = WorkloadMaterialAsset(index % 2 == 0 ? 0 : index) });
            _ = node.AddComponent(geometry);
            var position = new Vector3(index % 10, index / 10, 0);
            position *= 2;
            position -= new Vector3(9, 9, 0);
            node.Transform.LocalPosition = position;
            if (index is > 0 and < 8)
            {
                node.Hierarchy.SetParent(scene.RootNodes[0]);
            }
            else
            {
                scene.RootNodes.Add(node);
            }
        }

        var camera = new SceneNode(scene, "Camera");
        _ = camera.AddComponent(new PerspectiveCamera { Name = "Camera" });
        camera.Transform.LocalPosition = new Vector3(0, -25, 15);
        scene.RootNodes.Add(camera);
        var sun = new SceneNode(scene, "Sun");
        _ = sun.AddComponent(new DirectionalLightComponent { Name = "Sun", CastsShadows = true, CascadeCount = 4 });
        sun.Transform.LocalRotation = DirectionalLightComponent.DefaultLocalRotation;
        scene.RootNodes.Add(sun);
        sun.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary;
        scene.Hydrate(scene.Dehydrate() with { Environment = scene.Environment with { PostProcess = scene.Environment.PostProcess with { ManualExposureEv = 9.7f } } });
        _ = scene.RootNodes.SelectMany(static node => node.DescendantsAndSelf()).Should().HaveCount(100).And.OnlyContain(static node => node.Flags.IsVisible);
    }

    internal static string DescribeWorkloadCook(ContentCookResult result) => string.Join(Environment.NewLine, result.Diagnostics.Select(static issue =>
    {
        var details = issue.TechnicalMessage ?? string.Empty;
        return issue.Code + ": " + issue.Message + Environment.NewLine + details[..Math.Min(details.Length, 2048)];
    }));

    internal static string WorkloadMaterialPath(int index) => string.Create(CultureInfo.InvariantCulture, $"Content/Materials/Group{index % 10}/M{index:D4}.omat.json");

    internal static AssetPath WorkloadMaterialAsset(int index) => AssetPath.Parse(string.Create(CultureInfo.InvariantCulture, $"/Content/Materials/Group{index % 10}/M{index:D4}.omat"));
}
