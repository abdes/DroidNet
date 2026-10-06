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
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class CatalogWorkload
{
    internal static async Task SeedCatalogWorkloadAsync(NativeSceneFixture fixture, ProjectContext project, ContentPipelineService pipeline, BuiltinGeometryCatalog builtins, CancellationToken cancellationToken)
    {
        var request = new SceneImportRequest(project, Path.Combine(AppContext.BaseDirectory, "Fixtures/static_scalar_triangle.gltf"), "Triangle", new Uri("asset:///Content/Models"));
        var imported = await pipeline.ImportSourceAsync(request, cancellationToken).ConfigureAwait(true);
        _ = imported.IsPublished.Should().BeTrue(DescribeWorkloadCook(imported));
        var mesh = imported.CookedAssets.Single(static asset => asset.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        for (var index = 0; index < 998; index++)
        {
            var relative = WorkloadMaterialPath(index);
            var path = Path.Combine(fixture.ProjectRoot, relative);
            _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
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
            await File.WriteAllTextAsync(path, source, cancellationToken).ConfigureAwait(true);
        }

        var scene = fixture.Source;
        var inventories = builtins.AuthoringGeometries.ToDictionary(static definition => definition.AssetUri, static definition => definition.MaterialSlots);
        var importedSlots = await ((IGeometryMaterialSlotProvider)pipeline).ReadAsync(project, mesh, cancellationToken).ConfigureAwait(true);
        _ = importedSlots.Should().NotBeNull();
        inventories.Add(mesh, importedSlots!);
        SeedCatalogWorkloadScene(scene, builtins, mesh, inventories);
        var manager = new ProjectManagerService(new DroidNet.Storage.Native.NativeStorageProvider(new Testably.Abstractions.RealFileSystem()));
        _ = (await manager.SaveSceneAsync(scene).ConfigureAwait(true)).Should().BeTrue();
        var cooked = await pipeline.CookProjectAsync(cancellationToken).ConfigureAwait(true);
        _ = cooked.IsPublished.Should().BeTrue(DescribeWorkloadCook(cooked));
    }

    internal static void SeedCatalogWorkloadScene(Scene scene, BuiltinGeometryCatalog builtins, Uri mesh, IReadOnlyDictionary<Uri, GeometryMaterialSlotMetadata> inventories)
    {
        var authoringGeometries = builtins.AuthoringGeometries.ToArray();
        for (var index = 0; index < 98; index++)
        {
            var node = new SceneNode(scene)
            {
                Name = "Geometry " + index.ToString(CultureInfo.InvariantCulture),
                IsActive = true,
                CastsShadows = true,
                ReceivesShadows = true,
            };
            var geometryUri = index >= 90 ? mesh : authoringGeometries[index % authoringGeometries.Length].AssetUri;
            var geometry = new GeometryComponent
            {
                Name = "Geometry",
                Geometry = new AssetReference<GeometryAsset>(geometryUri),
            };
            var inventory = inventories[geometryUri];
            var target = new MaterialSlotTarget(geometryUri, inventory.Slots.Should().ContainSingle().Which.SlotId, inventory.LayoutRevision);
            geometry.OverrideSlots.Add(new MaterialsSlot { Target = target, Material = new AssetReference<MaterialAsset>(new Uri("asset:///" + WorkloadMaterialPath(index % 2 == 0 ? 0 : index))) });
            _ = node.AddComponent(geometry);
            var position = new Vector3(index % 10, index / 10, 0);
            position *= 2;
            position -= new Vector3(9, 9, 0);
            node.Components.OfType<TransformComponent>().Single().LocalPosition = position;
            if (index is > 0 and < 8)
            {
                node.SetParent(scene.RootNodes[0]);
            }
            else
            {
                scene.RootNodes.Add(node);
            }
        }

        var camera = new SceneNode(scene)
        {
            Name = "Camera",
            IsActive = true,
        };
        _ = camera.AddComponent(new PerspectiveCamera { Name = "Camera" });
        camera.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(0, -25, 15);
        scene.RootNodes.Add(camera);
        var sun = new SceneNode(scene)
        {
            Name = "Sun",
            IsActive = true,
        };
        _ = sun.AddComponent(new DirectionalLightComponent { Name = "Sun", CastsShadows = true, CascadeCount = 4 });
        sun.Components.OfType<TransformComponent>().Single().LocalRotation = DirectionalLightComponent.DefaultLocalRotation;
        scene.RootNodes.Add(sun);
        sun.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary;
        scene.Hydrate(scene.Dehydrate() with { Environment = scene.Environment with { PostProcess = scene.Environment.PostProcess with { ManualExposureEv = 9.7f } } });
        _ = scene.RootNodes.SelectMany(static node => node.Descendants().Prepend(node)).Should().HaveCount(100).And.OnlyContain(static node => node.IsActive && node.IsVisible);
    }

    internal static string DescribeWorkloadCook(ContentCookResult result) => string.Join(Environment.NewLine, result.Diagnostics.Select(static issue =>
    {
        var details = issue.TechnicalMessage ?? string.Empty;
        return issue.Code + ": " + issue.Message + Environment.NewLine + details[..Math.Min(details.Length, 2048)];
    }));

    internal static string WorkloadMaterialPath(int index) => string.Create(CultureInfo.InvariantCulture, $"Content/Materials/Group{index % 10}/M{index:D4}.omat.json");
}
