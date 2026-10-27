// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class IncrementalCookScenario
{
    internal static RecordingNativeApi CreateRecordingApi(INativeCompatibilityService compatibility) => new(new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility));

    internal static ContentPipelineService CreateIncrementalService(CookWorkspace workspace, RecordingNativeApi api, INativeCompatibilityService compatibility) => CreateService(workspace, api, compatibility: compatibility);

    internal static void AssertCookSucceeded(ContentCookResult result) => _ = result.Status.Should().BeOneOf([OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings], string.Join(Environment.NewLine, result.Diagnostics.Select(static diagnostic => diagnostic.TechnicalMessage ?? diagnostic.Message)));

    internal static Dictionary<string, (string Hash, DateTime Write)> ReadOutputIdentities(string projectRoot)
    {
        var headPath = global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Head(projectRoot);
        if (!File.Exists(headPath))
        {
            return [with(StringComparer.Ordinal)];
        }

        var head = System.Text.Json.JsonSerializer.Deserialize<global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationHead>(File.ReadAllBytes(headPath), global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationDocument.JsonOptions)!;
        var document = System.Text.Json.JsonSerializer.Deserialize<global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationDocument>(File.ReadAllBytes(global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Document(projectRoot, head.PublicationId)), global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationDocument.JsonOptions)!;
        return document.Roots.Where(static root => root.Owner == global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationRootOwner.Project).SelectMany(root => Directory.EnumerateFiles(root.ResolvePath(projectRoot), "*", SearchOption.AllDirectories).Where(static path => !string.Equals(Path.GetFileName(path), global::Oxygen.Editor.ContentPipeline.Publication.CookedGeneration.MarkerFileName, StringComparison.Ordinal)).Select(path => (Key: root.Name + "/" + Path.GetRelativePath(root.ResolvePath(projectRoot), path).Replace('\\', '/'), Path: path))).ToDictionary(static file => file.Key, static file => (Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file.Path))), File.GetLastWriteTimeUtc(file.Path)), StringComparer.Ordinal);
    }

    internal static async Task PrepareIncrementalSceneAsync(CookWorkspace workspace, ICookedGeometryInspector inspector)
    {
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        var node = new SceneNode(workspace.Scene, "Cube");
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = EngineAssets.BasicShape("Cube") };
        await ApplySurfaceMaterialOverrideAsync(
            workspace, inspector, geometry, AssetPath.Parse("/Content/Materials/Blue.omat"), workspace.Session.Lifetime).ConfigureAwait(false);
        _ = node.AddComponent(geometry);
        workspace.Scene.RootNodes.Add(node);
        workspace.Scene.SetEnvironment(new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto } });
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
    }

    // Reads the surface of a single-surface engine geometry from the workspace's engine content pak, as the inspector does,
    // and overrides its material on the geometry component.
    internal static async Task ApplySurfaceMaterialOverrideAsync(CookWorkspace workspace, ICookedGeometryInspector inspector, GeometryComponent component, AssetPath material, CancellationToken cancellationToken = default)
    {
        var geometry = component.Geometry;
        var report = await inspector.InspectGeometryAsync(
            Path.Combine(workspace.Root, ".build", "inspection"), workspace.EngineContent.Path.FullPath, geometry.VirtualPath, cancellationToken).ConfigureAwait(false);
        var inventory = report.SingleOrDefault(geometry)
            ?? throw new InvalidOperationException("The engine content pak has no geometry " + geometry.VirtualPath + ".");
        component.Overrides = component.Overrides.With(
            SceneSchema.Geometry.Surface.Material,
            new PartId(SceneSchema.Geometry.Surface.Kind, inventory.Surfaces.Single().SurfaceId),
            inventory.LayoutRevision,
            material);
    }
}
