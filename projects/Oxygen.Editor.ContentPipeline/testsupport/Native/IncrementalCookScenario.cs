// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class IncrementalCookScenario
{
    internal static RecordingNativeApi CreateRecordingApi(INativeCompatibilityService compatibility) => new(new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility));
    internal static ContentPipelineService CreateIncrementalService(CookWorkspace workspace, RecordingNativeApi api, INativeCompatibilityService compatibility) => CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
    internal static void AssertCookSucceeded(ContentCookResult result) => _ = result.Status.Should().BeOneOf([OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings], string.Join(Environment.NewLine, result.Diagnostics.Select(static diagnostic => diagnostic.TechnicalMessage ?? diagnostic.Message)));
    internal static Dictionary<string, (string hash, DateTime write)> ReadOutputIdentities(string projectRoot)
    {
        var headPath = global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Head(projectRoot);
        if (!File.Exists(headPath))
        {
            return [];
        }

        var head = System.Text.Json.JsonSerializer.Deserialize<global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationHead>(File.ReadAllBytes(headPath), global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationDocument.JsonOptions)!;
        var document = System.Text.Json.JsonSerializer.Deserialize<global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationDocument>(File.ReadAllBytes(global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Document(projectRoot, head.PublicationId)), global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationDocument.JsonOptions)!;
        return document.Roots.Where(static root => root.Owner == global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationRootOwner.Project).SelectMany(root => Directory.EnumerateFiles(root.ResolvePath(projectRoot), "*", SearchOption.AllDirectories).Where(static path => !string.Equals(Path.GetFileName(path), global::Oxygen.Editor.ContentPipeline.Publication.CookedGeneration.MarkerFileName, StringComparison.Ordinal)).Select(path => (Key: root.Name + "/" + Path.GetRelativePath(root.ResolvePath(projectRoot), path).Replace('\\', '/'), Path: path))).ToDictionary(static file => file.Key, static file => (Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file.Path))), File.GetLastWriteTimeUtc(file.Path)), StringComparer.Ordinal);
    }

    internal static async Task PrepareIncrementalSceneAsync(CookWorkspace workspace)
    {
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        var node = new SceneNode(workspace.Scene) { Name = "Cube" };
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")) };
        geometry.OverrideSlots.Add(await CreateBuiltinMaterialSlotAsync(
            workspace, geometry, new("asset:///Content/Materials/Blue.omat.json")).ConfigureAwait(false));
        _ = node.AddComponent(geometry);
        workspace.Scene.RootNodes.Add(node);
        workspace.Scene.SetEnvironment(new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto } });
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
    }

    internal static async Task<MaterialsSlot> CreateBuiltinMaterialSlotAsync(CookWorkspace workspace, GeometryComponent geometry, Uri materialUri, CancellationToken cancellationToken = default)
    {
        var catalog = await new BuiltinCatalogFixture().GetBuiltinGeometryCatalogAsync(
            workspace.Root, "Content", cancellationToken).ConfigureAwait(false);
        var geometryUri = geometry.Geometry?.Uri
            ?? throw new InvalidOperationException("The native fixture requires a geometry reference.");
        var inventory = (catalog.Find(geometryUri)
            ?? throw new InvalidOperationException("The native fixture has no matching builtin geometry.")).MaterialSlots;
        return new MaterialsSlot
        {
            Target = new(geometryUri, inventory.Slots.Single().SlotId, inventory.LayoutRevision),
            Material = new AssetReference<MaterialAsset>(materialUri),
        };
    }
}
