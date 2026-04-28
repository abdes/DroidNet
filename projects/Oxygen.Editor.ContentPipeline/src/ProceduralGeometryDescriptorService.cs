// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Generates deterministic native geometry descriptors for editor built-in procedural shapes.
/// </summary>
public sealed class ProceduralGeometryDescriptorService : IProceduralGeometryDescriptorService
{
    private const string DefaultMaterialName = "OxygenEditor_Default";
    private const string DefaultMaterialRef = $"/{AssetUris.ContentMountPoint}/Materials/{DefaultMaterialName}.omat";

    /// <inheritdoc />
    public async Task<IReadOnlyList<ContentCookInput>> EnsureDescriptorsAsync(
        ContentCookScope scope,
        IReadOnlyList<Uri> geometryUris,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(scope);
        ArgumentNullException.ThrowIfNull(geometryUris);

        var generated = new List<ContentCookInput>();
        var wroteDefaultMaterial = false;
        foreach (var uri in geometryUris.Distinct())
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!TryCreateDescriptor(uri, out var descriptor, out var stableName, out var outputVirtualPath))
            {
                continue;
            }

            if (!wroteDefaultMaterial)
            {
                generated.Add(await WriteDefaultMaterialAsync(scope, cancellationToken).ConfigureAwait(false));
                wroteDefaultMaterial = true;
            }

            var relativePath = Path.Combine(".pipeline", "Geometry", stableName + ".ogeo.json");
            var absolutePath = Path.Combine(scope.Project.ProjectRoot, relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(absolutePath)!);
            using (var stream = File.Create(absolutePath))
            {
                await JsonSerializer.SerializeAsync(
                    stream,
                    descriptor,
                    SceneDescriptorJson.Options,
                    cancellationToken).ConfigureAwait(false);
            }

            generated.Add(new ContentCookInput(
                uri,
                ContentCookAssetKind.Geometry,
                AssetUris.ContentMountPoint,
                NormalizeRelativePath(relativePath),
                absolutePath,
                outputVirtualPath,
                ContentCookInputRole.GeneratedDescriptor));
        }

        return generated;
    }

    /// <summary>
    /// Returns true when the URI identifies an editor/engine generated basic shape.
    /// </summary>
    /// <param name="uri">The asset URI.</param>
    /// <returns><see langword="true"/> when the URI identifies a supported generated basic shape.</returns>
    internal static bool IsGeneratedBasicShape(Uri uri)
        => string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
           && uri.AbsolutePath.StartsWith("/Engine/Generated/BasicShapes/", StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// Gets the cooked virtual path that corresponds to a generated basic shape.
    /// </summary>
    /// <param name="uri">The asset URI.</param>
    /// <param name="outputVirtualPath">The generated cooked virtual path.</param>
    /// <returns><see langword="true"/> when a virtual path was produced.</returns>
    internal static bool TryGetOutputVirtualPath(Uri uri, out string outputVirtualPath)
    {
        if (TryGetShape(uri, out var shape))
        {
            outputVirtualPath = $"/{AssetUris.ContentMountPoint}/Geometry/Engine_Generated_BasicShapes_{shape}.ogeo";
            return true;
        }

        outputVirtualPath = string.Empty;
        return false;
    }

    private static bool TryCreateDescriptor(
        Uri uri,
        out NativeGeometryDescriptor descriptor,
        out string stableName,
        out string outputVirtualPath)
    {
        descriptor = null!;
        stableName = string.Empty;
        outputVirtualPath = string.Empty;
        if (!TryGetShape(uri, out var shape))
        {
            return false;
        }

        stableName = "Engine_Generated_BasicShapes_" + shape;
        outputVirtualPath = $"/{AssetUris.ContentMountPoint}/Geometry/{stableName}.ogeo";
        var bounds = GetBounds(shape);
        descriptor = new NativeGeometryDescriptor(
            Schema: "oxygen.geometry-descriptor.v1",
            Name: stableName,
            Bounds: bounds,
            Lods:
            [
                new NativeGeometryLod(
                    Name: "LOD0",
                    MeshType: "procedural",
                    Bounds: bounds,
                    Procedural: new NativeProceduralDescriptor(
                        Generator: shape,
                        MeshName: shape,
                        Params: GetParams(shape)),
                    Submeshes:
                    [
                        new NativeSubmeshDescriptor(
                            Name: "Main",
                            MaterialRef: DefaultMaterialRef,
                            Views: [new NativeSubmeshView("__all__")]),
                    ]),
            ]);
        return true;
    }

    private static async Task<ContentCookInput> WriteDefaultMaterialAsync(
        ContentCookScope scope,
        CancellationToken cancellationToken)
    {
        var relativePath = Path.Combine(".pipeline", "Materials", DefaultMaterialName + ".omat.json");
        var absolutePath = Path.Combine(scope.Project.ProjectRoot, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(absolutePath)!);

        var descriptor = new NativeMaterialDescriptor(
            Name: DefaultMaterialName,
            Domain: "opaque",
            AlphaMode: "opaque",
            Parameters: new NativeMaterialParameters(
                BaseColor: [1.0f, 1.0f, 1.0f, 1.0f],
                Metalness: 0.0f,
                Roughness: 0.5f,
                DoubleSided: false,
                AlphaCutoff: null));

        using (var stream = File.Create(absolutePath))
        {
            await JsonSerializer.SerializeAsync(
                stream,
                descriptor,
                SceneDescriptorJson.Options,
                cancellationToken).ConfigureAwait(false);
        }

        return new ContentCookInput(
            new Uri($"asset:///{AssetUris.ContentMountPoint}/Materials/{DefaultMaterialName}.omat.json"),
            ContentCookAssetKind.Material,
            AssetUris.ContentMountPoint,
            NormalizeRelativePath(relativePath),
            absolutePath,
            DefaultMaterialRef,
            ContentCookInputRole.GeneratedDescriptor);
    }

    private static bool TryGetShape(Uri uri, out string shape)
    {
        shape = string.Empty;
        if (!IsGeneratedBasicShape(uri))
        {
            return false;
        }

        var name = Uri.UnescapeDataString(uri.AbsolutePath["/Engine/Generated/BasicShapes/".Length..]);
        shape = name switch
        {
            "Cube" => "Cube",
            "Sphere" => "Sphere",
            "Plane" => "Plane",
            _ => string.Empty,
        };
        return shape.Length > 0;
    }

    private static NativeBounds GetBounds(string shape)
        => shape switch
        {
            "Plane" => new NativeBounds([-0.5f, 0.0f, -0.5f], [0.5f, 0.0f, 0.5f]),
            _ => new NativeBounds([-0.5f, -0.5f, -0.5f], [0.5f, 0.5f, 0.5f]),
        };

    private static object? GetParams(string shape)
        => shape switch
        {
            "Cube" => new Dictionary<string, object>(StringComparer.Ordinal),
            "Sphere" => new NativeSphereParams(LatitudeSegments: 32, LongitudeSegments: 64),
            "Plane" => new NativePlaneParams(XSegments: 1, ZSegments: 1, Size: 1.0f),
            _ => null,
        };

    private static string NormalizeRelativePath(string path)
        => path.Replace('\\', '/');
}
