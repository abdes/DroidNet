// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Numerics;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

internal static class EngineImportDescriptorCooker
{
    private const string ImportToolFileName = "Oxygen.Cooker.ImportTool.exe";
    private const string GeneratedRoot = "Content/Generated/OxygenEditor";
    private const string DefaultMaterialName = "OxygenEditorDefault";
    private const string DefaultMaterialRef = "/.cooked/Materials/OxygenEditorDefault.omat";
    private const string GeneratedBasicShapesPrefix = "asset:///Engine/Generated/BasicShapes/";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
    };

    public static async Task CookSceneAsync(Scene scene, ILogger logger)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(logger);

        var projectRoot = scene.Project.ProjectInfo.Location;
        if (string.IsNullOrWhiteSpace(projectRoot) || !Path.IsPathFullyQualified(projectRoot))
        {
            logger.LogWarning("Skipping scene cook for {Scene}: project root is not an absolute path.", scene.Name);
            return;
        }

        var importTool = FindImportTool(projectRoot);
        if (importTool is null)
        {
            throw new FileNotFoundException(
                $"Could not locate {ImportToolFileName}. Build or install Oxygen.Cooker.ImportTool and ensure it is on PATH or under projects/Oxygen.Engine/out.");
        }

        var descriptorSet = BuildDescriptorSet(scene);
        await WriteDescriptorSetAsync(projectRoot, descriptorSet).ConfigureAwait(false);
        await RunImportToolAsync(projectRoot, importTool, descriptorSet.ManifestPath, logger).ConfigureAwait(false);
        CleanupLegacyEditorCookedRoot(projectRoot, descriptorSet.SceneName, logger);
    }

    private static DescriptorSet BuildDescriptorSet(Scene scene)
    {
        var sceneName = ToIdentifier(scene.Name, fallback: "Scene");
        var generated = new Dictionary<string, GeneratedGeometry>(StringComparer.Ordinal);
        var nodeIndices = new Dictionary<SceneNode, int>();
        var nodes = new List<Dictionary<string, object?>>();
        var renderables = new List<Dictionary<string, object?>>();
        var perspectiveCameras = new List<Dictionary<string, object?>>();
        var directionalLights = new List<Dictionary<string, object?>>();
        var pointLights = new List<Dictionary<string, object?>>();
        var spotLights = new List<Dictionary<string, object?>>();

        foreach (var root in scene.RootNodes)
        {
            AddNode(root, parentIndex: null);
        }

        var sceneDescriptor = new Dictionary<string, object?>
        {
            ["version"] = 3,
            ["name"] = sceneName,
            ["nodes"] = nodes,
            ["environment"] = CreateDefaultEnvironment(),
        };

        if (renderables.Count > 0)
        {
            sceneDescriptor["renderables"] = renderables;
        }

        if (perspectiveCameras.Count > 0)
        {
            sceneDescriptor["cameras"] = new Dictionary<string, object?>
            {
                ["perspective"] = perspectiveCameras,
            };
        }

        var lights = new Dictionary<string, object?>();
        if (directionalLights.Count > 0)
        {
            lights["directional"] = directionalLights;
        }

        if (pointLights.Count > 0)
        {
            lights["point"] = pointLights;
        }

        if (spotLights.Count > 0)
        {
            lights["spot"] = spotLights;
        }

        if (lights.Count > 0)
        {
            sceneDescriptor["lights"] = lights;
        }

        if (generated.Count > 0)
        {
            sceneDescriptor["references"] = new Dictionary<string, object?>
            {
                ["materials"] = new[] { DefaultMaterialRef },
            };
        }

        var geometries = generated.Values.OrderBy(static g => g.Name, StringComparer.Ordinal).ToList();
        var manifest = CreateImportManifest(sceneName, geometries);

        return new DescriptorSet(
            ManifestPath: Path.Combine(GeneratedRoot, $"import-manifest.{sceneName}.json"),
            MaterialDescriptorPath: Path.Combine(GeneratedRoot, "Materials", $"{DefaultMaterialName}.material.json"),
            SceneDescriptorPath: Path.Combine(GeneratedRoot, "Scenes", $"{sceneName}.scene.json"),
            SceneName: sceneName,
            MaterialDescriptor: CreateDefaultMaterialDescriptor(),
            GeometryDescriptors: geometries
                .Select(g => new DescriptorFile(
                    Path.Combine(GeneratedRoot, "Geometry", $"{g.Name}.geometry.json"),
                    CreateProceduralGeometryDescriptor(g)))
                .ToList(),
            SceneDescriptor: sceneDescriptor,
            Manifest: manifest);

        void AddNode(SceneNode node, int? parentIndex)
        {
            var nodeIndex = nodes.Count;
            nodeIndices[node] = nodeIndex;

            var transform = node.Components.OfType<TransformComponent>().FirstOrDefault();
            var nodeDescriptor = new Dictionary<string, object?>
            {
                ["name"] = ToIdentifier(node.Name, fallback: $"Node{nodeIndex}"),
            };

            if (parentIndex.HasValue)
            {
                nodeDescriptor["parent"] = parentIndex.Value;
            }

            nodeDescriptor["flags"] = new Dictionary<string, object?>
            {
                ["visible"] = node.IsVisible,
                ["static"] = node.IsStatic,
                ["casts_shadows"] = node.CastsShadows,
                ["receives_shadows"] = node.ReceivesShadows,
                ["ray_cast_selectable"] = node.IsRayCastingSelectable,
                ["ignore_parent_transform"] = node.IgnoreParentTransform,
            };

            if (transform is not null)
            {
                nodeDescriptor["transform"] = new Dictionary<string, object?>
                {
                    ["translation"] = ToArray(transform.LocalPosition),
                    ["rotation"] = ToArray(transform.LocalRotation),
                    ["scale"] = ToArray(transform.LocalScale),
                };
            }

            nodes.Add(nodeDescriptor);

            if (node.Components.OfType<GeometryComponent>().FirstOrDefault() is { } geometry
                && TryGetGeneratedGeometry(geometry, out var generatedGeometry))
            {
                generated.TryAdd(generatedGeometry.VirtualPath, generatedGeometry);
                renderables.Add(new Dictionary<string, object?>
                {
                    ["node"] = nodeIndex,
                    ["geometry_ref"] = generatedGeometry.CookedGeometryRef,
                    ["visible"] = true,
                });
            }

            if (node.Components.OfType<PerspectiveCamera>().FirstOrDefault() is { } camera)
            {
                perspectiveCameras.Add(new Dictionary<string, object?>
                {
                    ["node"] = nodeIndex,
                    ["fov_y"] = ToRadians(camera.FieldOfView),
                    ["aspect_ratio"] = camera.AspectRatio,
                    ["near_plane"] = camera.NearPlane,
                    ["far_plane"] = camera.FarPlane,
                });
            }

            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is { } directional)
            {
                directionalLights.Add(new Dictionary<string, object?>
                {
                    ["node"] = nodeIndex,
                    ["common"] = CreateLightCommon(directional),
                    ["angular_size_radians"] = Math.Max(directional.AngularSizeRadians, 0.00935f),
                    ["environment_contribution"] = directional.EnvironmentContribution,
                    ["is_sun_light"] = directional.IsSunLight,
                    ["intensity_lux"] = Math.Max(directional.IntensityLux, 100000.0f),
                });
            }

            if (node.Components.OfType<PointLightComponent>().FirstOrDefault() is { } point)
            {
                pointLights.Add(new Dictionary<string, object?>
                {
                    ["node"] = nodeIndex,
                    ["common"] = CreateLightCommon(point),
                    ["range"] = point.Range,
                    ["attenuation_model"] = 0,
                    ["decay_exponent"] = point.DecayExponent,
                    ["source_radius"] = point.SourceRadius,
                    ["luminous_flux_lm"] = point.LuminousFluxLumens,
                });
            }

            if (node.Components.OfType<SpotLightComponent>().FirstOrDefault() is { } spot)
            {
                spotLights.Add(new Dictionary<string, object?>
                {
                    ["node"] = nodeIndex,
                    ["common"] = CreateLightCommon(spot),
                    ["range"] = spot.Range,
                    ["attenuation_model"] = 0,
                    ["decay_exponent"] = spot.DecayExponent,
                    ["inner_cone_angle_radians"] = spot.InnerConeAngleRadians,
                    ["outer_cone_angle_radians"] = spot.OuterConeAngleRadians,
                    ["source_radius"] = spot.SourceRadius,
                    ["luminous_flux_lm"] = spot.LuminousFluxLumens,
                });
            }

            foreach (var child in node.Children)
            {
                AddNode(child, nodeIndex);
            }
        }
    }

    private static Dictionary<string, object?> CreateImportManifest(string sceneName, IReadOnlyList<GeneratedGeometry> geometries)
    {
        var jobs = new List<Dictionary<string, object?>>
        {
            new()
            {
                ["id"] = $"material.{DefaultMaterialName}",
                ["type"] = "material-descriptor",
                ["source"] = $"./Materials/{DefaultMaterialName}.material.json",
                ["name"] = DefaultMaterialName,
                ["content_hashing"] = true,
            },
        };

        foreach (var geometry in geometries)
        {
            jobs.Add(new Dictionary<string, object?>
            {
                ["id"] = $"geometry.{geometry.Name}",
                ["type"] = "geometry-descriptor",
                ["source"] = $"./Geometry/{geometry.Name}.geometry.json",
                ["name"] = geometry.Name,
                ["content_hashing"] = true,
                ["depends_on"] = new[] { $"material.{DefaultMaterialName}" },
            });
        }

        jobs.Add(new Dictionary<string, object?>
        {
            ["id"] = $"scene.{sceneName}",
            ["type"] = "scene-descriptor",
            ["source"] = $"./Scenes/{sceneName}.scene.json",
            ["name"] = sceneName,
            ["content_hashing"] = true,
            ["depends_on"] = geometries.Select(g => $"geometry.{g.Name}").ToArray(),
        });

        return new Dictionary<string, object?>
        {
            ["version"] = 1,
            ["max_in_flight_jobs"] = 1,
            ["output"] = "../../../.cooked",
            ["jobs"] = jobs,
        };
    }

    private static Dictionary<string, object?> CreateDefaultMaterialDescriptor()
        => new()
        {
            ["name"] = DefaultMaterialName,
            ["domain"] = "opaque",
            ["alpha_mode"] = "opaque",
            ["parameters"] = new Dictionary<string, object?>
            {
                ["base_color"] = new[] { 0.8f, 0.8f, 0.8f, 1.0f },
                ["metalness"] = 0.0f,
                ["roughness"] = 0.55f,
                ["ambient_occlusion"] = 1.0f,
            },
        };

    private static Dictionary<string, object?> CreateProceduralGeometryDescriptor(GeneratedGeometry geometry)
        => new()
        {
            ["name"] = geometry.Name,
            ["bounds"] = UnitBounds(),
            ["lods"] = new[]
            {
                new Dictionary<string, object?>
                {
                    ["name"] = $"{geometry.Name}_lod0",
                    ["mesh_type"] = "procedural",
                    ["bounds"] = UnitBounds(),
                    ["procedural"] = new Dictionary<string, object?>
                    {
                        ["generator"] = geometry.Generator,
                        ["mesh_name"] = geometry.Name,
                    },
                    ["submeshes"] = new[]
                    {
                        new Dictionary<string, object?>
                        {
                            ["name"] = $"{geometry.Name}_surface",
                            ["material_ref"] = DefaultMaterialRef,
                            ["views"] = new[]
                            {
                                new Dictionary<string, object?>
                                {
                                    ["view_ref"] = "__all__",
                                },
                            },
                        },
                    },
                },
            },
        };

    private static Dictionary<string, object?> CreateDefaultEnvironment()
        => new()
        {
            ["sky_atmosphere"] = new Dictionary<string, object?>
            {
                ["enabled"] = true,
                ["planet_radius_m"] = 6360000.0f,
                ["atmosphere_height_m"] = 100000.0f,
                ["ground_albedo_rgb"] = new[] { 0.1f, 0.1f, 0.1f },
                ["rayleigh_scattering_rgb"] = new[] { 5.802e-06f, 1.3558e-05f, 3.31e-05f },
                ["rayleigh_scale_height_m"] = 8000.0f,
                ["mie_scattering_rgb"] = new[] { 3.996e-06f, 3.996e-06f, 3.996e-06f },
                ["mie_absorption_rgb"] = new[] { 4.4e-06f, 4.4e-06f, 4.4e-06f },
                ["mie_scale_height_m"] = 1200.0f,
                ["mie_anisotropy"] = 0.8f,
                ["ozone_absorption_rgb"] = new[] { 6.5e-07f, 1.881e-06f, 8.5e-08f },
                ["ozone_density_profile"] = new[] { 25000.0f, 0.0f, 0.0f },
                ["multi_scattering_factor"] = 1.0f,
                ["sky_luminance_factor_rgb"] = new[] { 1.0f, 1.0f, 1.0f },
                ["sky_and_aerial_perspective_luminance_factor_rgb"] = new[] { 1.0f, 1.0f, 1.0f },
                ["aerial_perspective_distance_scale"] = 1.0f,
                ["aerial_scattering_strength"] = 1.0f,
                ["aerial_perspective_start_depth_m"] = 100.0f,
                ["height_fog_contribution"] = 1.0f,
                ["trace_sample_count_scale"] = 1.0f,
                ["transmittance_min_light_elevation_deg"] = -90.0f,
                ["sun_disk_enabled"] = true,
                ["holdout"] = false,
                ["render_in_main_pass"] = true,
            },
        };

    private static Dictionary<string, object?> CreateLightCommon(LightComponent light)
        => new()
        {
            ["affects_world"] = light.AffectsWorld,
            ["color_rgb"] = new[] { light.Color.X, light.Color.Y, light.Color.Z },
            ["casts_shadows"] = light.CastsShadows,
            ["exposure_compensation_ev"] = light.ExposureCompensation,
        };

    private static bool TryGetGeneratedGeometry(GeometryComponent component, out GeneratedGeometry geometry)
    {
        geometry = default;
        var uri = component.Geometry?.Uri?.ToString();
        if (string.IsNullOrWhiteSpace(uri)
            || !uri.StartsWith(GeneratedBasicShapesPrefix, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        var requestedGenerator = uri[GeneratedBasicShapesPrefix.Length..];
        var generator = requestedGenerator.ToLowerInvariant() switch
        {
            "cube" => "Cube",
            "subdividedcube" => "SubdividedCube",
            "arrowgizmo" => "ArrowGizmo",
            "sphere" => "Sphere",
            "icosphere" => "IcoSphere",
            "geodesicsphere" => "GeodesicSphere",
            "plane" => "Plane",
            "cylinder" => "Cylinder",
            "cone" => "Cone",
            "torus" => "Torus",
            "quad" => "Quad",
            _ => null,
        };

        if (generator is null)
        {
            return false;
        }

        var name = ToIdentifier($"Generated{generator}", fallback: "GeneratedGeometry");
        geometry = new GeneratedGeometry(
            VirtualPath: uri,
            Generator: generator,
            Name: name,
            CookedGeometryRef: $"/.cooked/Geometry/{name}.ogeo");
        return true;
    }

    private static async Task WriteDescriptorSetAsync(string projectRoot, DescriptorSet descriptors)
    {
        await WriteJsonAsync(projectRoot, descriptors.MaterialDescriptorPath, descriptors.MaterialDescriptor).ConfigureAwait(false);
        foreach (var descriptor in descriptors.GeometryDescriptors)
        {
            await WriteJsonAsync(projectRoot, descriptor.RelativePath, descriptor.Payload).ConfigureAwait(false);
        }

        await WriteJsonAsync(projectRoot, descriptors.SceneDescriptorPath, descriptors.SceneDescriptor).ConfigureAwait(false);

        await WriteJsonAsync(projectRoot, descriptors.ManifestPath, descriptors.Manifest).ConfigureAwait(false);
    }

    private static async Task WriteJsonAsync(string projectRoot, string relativePath, object payload)
    {
        var path = Path.Combine(projectRoot, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var json = JsonSerializer.Serialize(payload, JsonOptions);
        await File.WriteAllTextAsync(path, json).ConfigureAwait(false);
    }

    private static async Task RunImportToolAsync(
        string projectRoot,
        string importTool,
        string manifestRelativePath,
        ILogger logger)
    {
        var manifestPath = Path.Combine(projectRoot, manifestRelativePath);
        var startInfo = new ProcessStartInfo(importTool)
        {
            WorkingDirectory = Path.GetDirectoryName(manifestPath)!,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        startInfo.ArgumentList.Add("--no-tui");
        startInfo.ArgumentList.Add("--no-color");
        startInfo.ArgumentList.Add("--fail-fast");
        startInfo.ArgumentList.Add("batch");
        startInfo.ArgumentList.Add("--manifest");
        startInfo.ArgumentList.Add(manifestPath);

        using var process = Process.Start(startInfo)
            ?? throw new InvalidOperationException($"Failed to start {ImportToolFileName}.");
        var stdoutTask = process.StandardOutput.ReadToEndAsync();
        var stderrTask = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync().ConfigureAwait(false);
        var stdout = await stdoutTask.ConfigureAwait(false);
        var stderr = await stderrTask.ConfigureAwait(false);

        if (!string.IsNullOrWhiteSpace(stdout))
        {
            logger.LogInformation("Oxygen cooker output: {Output}", stdout.Trim());
        }

        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException(
                $"{ImportToolFileName} failed with exit code {process.ExitCode}: {stderr.Trim()}");
        }
    }

    private static void CleanupLegacyEditorCookedRoot(string projectRoot, string sceneName, ILogger logger)
    {
        var legacyContentRoot = Path.Combine(projectRoot, ".cooked", "Content");
        var legacyIndexPath = Path.Combine(legacyContentRoot, "container.index.bin");
        var legacyScenePath = Path.Combine(legacyContentRoot, "Scenes", $"{sceneName}.oscene");

        TryDeleteFile(legacyScenePath, logger);
        TryDeleteFile(legacyIndexPath, logger);

        TryDeleteEmptyDirectory(Path.Combine(legacyContentRoot, "Scenes"), logger);
        TryDeleteEmptyDirectory(legacyContentRoot, logger);
    }

    private static void TryDeleteFile(string path, ILogger logger)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
                logger.LogInformation("Removed stale legacy cooked artifact: {Path}", path);
            }
        }
        catch (Exception ex) when (ex is IOException
            or UnauthorizedAccessException
            or ArgumentException
            or NotSupportedException)
        {
            logger.LogWarning(ex, "Failed to remove stale legacy cooked artifact: {Path}", path);
        }
    }

    private static void TryDeleteEmptyDirectory(string path, ILogger logger)
    {
        try
        {
            if (Directory.Exists(path) && !Directory.EnumerateFileSystemEntries(path).Any())
            {
                Directory.Delete(path);
                logger.LogInformation("Removed empty legacy cooked directory: {Path}", path);
            }
        }
        catch (Exception ex) when (ex is IOException
            or UnauthorizedAccessException
            or ArgumentException
            or NotSupportedException)
        {
            logger.LogWarning(ex, "Failed to remove empty legacy cooked directory: {Path}", path);
        }
    }

    private static string? FindImportTool(string projectRoot)
    {
        if (TryFindOnPath(ImportToolFileName) is { } pathTool)
        {
            return pathTool;
        }

        foreach (var start in new[] { AppContext.BaseDirectory, Environment.CurrentDirectory, projectRoot })
        {
            var dir = new DirectoryInfo(start);
            while (dir is not null)
            {
                var candidates = new[]
                {
                    Path.Combine(dir.FullName, "projects", "Oxygen.Engine", "out", "install", "Debug", "bin", ImportToolFileName),
                    Path.Combine(dir.FullName, "projects", "Oxygen.Engine", "out", "build-vs", "bin", "Debug", ImportToolFileName),
                    Path.Combine(dir.FullName, "projects", "Oxygen.Engine", "out", "build-vs", "bin", "Release", ImportToolFileName),
                    Path.Combine(dir.FullName, "projects", "Oxygen.Engine", "out", "build-ninja", "bin", "Debug", ImportToolFileName),
                    Path.Combine(dir.FullName, "projects", "Oxygen.Engine", "out", "build-ninja", "bin", "Release", ImportToolFileName),
                    Path.Combine(dir.FullName, "projects", "Oxygen.Engine", "out", "build-tracy-ninja", "bin", "Debug", ImportToolFileName),
                    Path.Combine(dir.FullName, "projects", "Oxygen.Engine", "out", "build-asan-vs", "bin", "Debug", ImportToolFileName),
                };

                foreach (var candidate in candidates)
                {
                    if (File.Exists(candidate))
                    {
                        return candidate;
                    }
                }

                dir = dir.Parent;
            }
        }

        return null;
    }

    private static string? TryFindOnPath(string fileName)
    {
        var path = Environment.GetEnvironmentVariable("PATH");
        if (string.IsNullOrWhiteSpace(path))
        {
            return null;
        }

        foreach (var dir in path.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
        {
            var candidate = Path.Combine(dir, fileName);
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return null;
    }

    private static Dictionary<string, object?> UnitBounds()
        => new()
        {
            ["min"] = new[] { -0.5f, -0.5f, -0.5f },
            ["max"] = new[] { 0.5f, 0.5f, 0.5f },
        };

    private static float ToRadians(float degrees)
        => degrees > 0.0f && float.IsFinite(degrees) ? degrees * (MathF.PI / 180.0f) : 60.0f * (MathF.PI / 180.0f);

    private static float[] ToArray(Vector3 value) => [value.X, value.Y, value.Z];

    private static float[] ToArray(Quaternion value) => [value.X, value.Y, value.Z, value.W];

    private static string ToIdentifier(string? value, string fallback)
    {
        var source = string.IsNullOrWhiteSpace(value) ? fallback : value;
        var chars = source
            .Select(static c => char.IsAsciiLetterOrDigit(c) || c is '_' or '.' or '-' ? c : '_')
            .ToArray();
        var result = new string(chars).Trim('_');
        if (string.IsNullOrWhiteSpace(result) || !char.IsAsciiLetterOrDigit(result[0]) && result[0] != '_')
        {
            result = fallback;
        }

        return result.Length <= 63 ? result : result[..63];
    }

    private readonly record struct GeneratedGeometry(
        string VirtualPath,
        string Generator,
        string Name,
        string CookedGeometryRef);

    private sealed record DescriptorFile(string RelativePath, object Payload);

    private sealed record DescriptorSet(
        string ManifestPath,
        string MaterialDescriptorPath,
        string SceneDescriptorPath,
        string SceneName,
        object MaterialDescriptor,
        IReadOnlyList<DescriptorFile> GeometryDescriptors,
        object SceneDescriptor,
        Dictionary<string, object?> Manifest);
}
