// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using Oxygen.Core;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.ContentPipeline;

#pragma warning disable MA0051 // Scene descriptor generation is a single schema mapping operation.

/// <summary>
/// Generates native Oxygen scene descriptors from editor scene documents.
/// </summary>
public sealed class SceneDescriptorGenerator : ISceneDescriptorGenerator
{
    private readonly IProceduralGeometryDescriptorService proceduralGeometryDescriptors;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneDescriptorGenerator"/> class.
    /// </summary>
    /// <param name="proceduralGeometryDescriptors">The generated geometry descriptor service.</param>
    public SceneDescriptorGenerator(IProceduralGeometryDescriptorService proceduralGeometryDescriptors)
        => this.proceduralGeometryDescriptors = proceduralGeometryDescriptors
           ?? throw new ArgumentNullException(nameof(proceduralGeometryDescriptors));

    /// <inheritdoc />
    public async Task<SceneDescriptorGenerationResult> GenerateAsync(
        Scene scene,
        ContentCookScope scope,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(scope);
        cancellationToken.ThrowIfCancellationRequested();

        var sceneInput = FindSceneInput(scope);
        var descriptorPath = GetDerivedSceneDescriptorPath(scope, sceneInput);
        var descriptorVirtualPath = ContentPipelinePaths.ToNativeDescriptorPath(sceneInput.AssetUri, ".oscene");
        var diagnostics = new List<DiagnosticRecord>();
        var operationId = Guid.NewGuid();
        if (!scene.RootNodes.Any())
        {
            diagnostics.Add(CreateDiagnostic(
                operationId,
                DiagnosticSeverity.Error,
                ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                $"Scene `{scene.Name}` cannot be cooked because it has no nodes.",
                descriptorPath,
                descriptorVirtualPath));
            return new SceneDescriptorGenerationResult(
                sceneInput.AssetUri,
                descriptorPath,
                descriptorVirtualPath,
                Dependencies: [],
                diagnostics);
        }

        var generatedGeometryUris = scene.AllNodes
            .SelectMany(static node => node.Components.OfType<GeometryComponent>())
            .Select(static geometry => geometry.Geometry?.Uri)
            .Where(static uri => uri is not null && ProceduralGeometryDescriptorService.IsGeneratedBasicShape(uri))
            .Cast<Uri>()
            .Distinct()
            .ToArray();
        var generatedGeometryInputs = await this.proceduralGeometryDescriptors
            .EnsureDescriptorsAsync(scope, generatedGeometryUris, cancellationToken)
            .ConfigureAwait(false);

        var nodes = new List<NativeSceneNode>();
        var renderables = new List<NativeRenderable>();
        var cameras = new List<NativePerspectiveCamera>();
        var directionalLights = new List<NativeDirectionalLight>();
        var pointLights = new List<NativePointLight>();
        var spotLights = new List<NativeSpotLight>();
        var materialRefs = new SortedSet<string>(StringComparer.Ordinal);
        var dependencyInputs = new List<ContentCookInput>(generatedGeometryInputs);
        dependencyInputs.AddRange(ResolveGeometryDependencies(scene, scope));
        dependencyInputs.AddRange(ResolveMaterialDependencies(scene, scope));
        AddEnvironmentDiagnostics();

        foreach (var root in scene.RootNodes)
        {
            AddNode(root, parentIndex: null);
        }

        var lights = directionalLights.Count == 0 && pointLights.Count == 0 && spotLights.Count == 0
            ? null
            : new NativeLights(
                Directional: directionalLights.Count == 0 ? null : directionalLights,
                Point: pointLights.Count == 0 ? null : pointLights,
                Spot: spotLights.Count == 0 ? null : spotLights);
        var descriptor = new NativeSceneDescriptor(
            Schema: "oxygen.scene-descriptor.v3",
            Version: 3,
            Name: ContentPipelinePaths.NormalizeSceneDescriptorName(Path.GetFileName(sceneInput.SourceRelativePath)),
            Nodes: nodes,
            Renderables: renderables.Count == 0 ? null : renderables,
            Cameras: cameras.Count == 0 ? null : new NativeCameras(cameras),
            Lights: lights,
            Environment: CreateEnvironment(scene.Environment.AtmosphereEnabled),
            References: materialRefs.Count == 0 ? null : new NativeReferences(materialRefs.ToArray(), ExtraAssets: null));

        Directory.CreateDirectory(Path.GetDirectoryName(descriptorPath)!);
        using (var stream = File.Create(descriptorPath))
        {
            await JsonSerializer.SerializeAsync(
                stream,
                descriptor,
                SceneDescriptorJson.Options,
                cancellationToken).ConfigureAwait(false);
        }

        return new SceneDescriptorGenerationResult(
            sceneInput.AssetUri,
            descriptorPath,
            descriptorVirtualPath,
            dependencyInputs,
            diagnostics);

        void AddNode(SceneNode node, int? parentIndex)
        {
            var nodeIndex = nodes.Count;
            var transform = node.Components.OfType<TransformComponent>().First();
            nodes.Add(new NativeSceneNode(
                node.Name,
                parentIndex,
                new NativeNodeFlags(
                    node.IsVisible,
                    node.IsStatic,
                    node.CastsShadows,
                    node.ReceivesShadows,
                    node.IsRayCastingSelectable,
                    node.IgnoreParentTransform),
                new NativeNodeTransform(
                    ToArray(transform.LocalPosition),
                    ToArray(transform.LocalRotation),
                    ToArray(transform.LocalScale))));

            foreach (var geometry in node.Components.OfType<GeometryComponent>())
            {
                AddRenderable(node, nodeIndex, geometry);
            }

            foreach (var camera in node.Components.OfType<PerspectiveCamera>())
            {
                cameras.Add(new NativePerspectiveCamera(
                    nodeIndex,
                    camera.FieldOfView,
                    camera.AspectRatio,
                    camera.NearPlane,
                    camera.FarPlane));
            }

            foreach (var camera in node.Components.OfType<OrthographicCamera>())
            {
                diagnostics.Add(CreateDiagnostic(
                    operationId,
                    DiagnosticSeverity.Warning,
                    ContentPipelineDiagnosticCodes.SceneUnsupportedField,
                    $"Scene node `{node.Name}` has orthographic camera `{camera.Name}`, which is not emitted by the ED-M07 descriptor slice.",
                    descriptorPath,
                    descriptorVirtualPath));
            }

            foreach (var light in node.Components.OfType<DirectionalLightComponent>())
            {
                directionalLights.Add(new NativeDirectionalLight(
                    nodeIndex,
                    ToCommon(light),
                    light.IntensityLux,
                    light.AngularSizeRadians,
                    light.EnvironmentContribution,
                    IsSceneSunLight(scene, node, light)));
            }

            foreach (var light in node.Components.OfType<PointLightComponent>())
            {
                pointLights.Add(new NativePointLight(
                    nodeIndex,
                    ToCommon(light),
                    light.LuminousFluxLumens,
                    light.Range,
                    light.SourceRadius,
                    light.DecayExponent));
            }

            foreach (var light in node.Components.OfType<SpotLightComponent>())
            {
                spotLights.Add(new NativeSpotLight(
                    nodeIndex,
                    ToCommon(light),
                    light.LuminousFluxLumens,
                    light.Range,
                    light.SourceRadius,
                    light.DecayExponent,
                    light.InnerConeAngleRadians,
                    light.OuterConeAngleRadians));
            }

            foreach (var child in node.Children)
            {
                AddNode(child, nodeIndex);
            }
        }

        void AddRenderable(SceneNode node, int nodeIndex, GeometryComponent geometry)
        {
            if (geometry.Geometry is null)
            {
                diagnostics.Add(CreateDiagnostic(
                    operationId,
                    DiagnosticSeverity.Error,
                    ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                    $"Scene node `{node.Name}` has a geometry component without a geometry asset.",
                    descriptorPath,
                    descriptorVirtualPath));
                return;
            }

            string? geometryRef;
            try
            {
                geometryRef = ResolveGeometryRef(geometry.Geometry.Uri, generatedGeometryInputs);
            }
            catch (ArgumentException ex)
            {
                diagnostics.Add(CreateDiagnostic(
                    operationId,
                    DiagnosticSeverity.Error,
                    ContentPipelineDiagnosticCodes.GeometryDescriptorGenerationFailed,
                    $"Scene node `{node.Name}` references geometry `{geometry.Geometry.Uri}` that cannot be normalized: {ex.Message}",
                    descriptorPath,
                    descriptorVirtualPath));
                return;
            }

            if (geometryRef is null)
            {
                diagnostics.Add(CreateDiagnostic(
                    operationId,
                    DiagnosticSeverity.Error,
                    ContentPipelineDiagnosticCodes.GeometryDescriptorGenerationFailed,
                    $"Scene node `{node.Name}` references unsupported geometry `{geometry.Geometry.Uri}`.",
                    descriptorPath,
                    descriptorVirtualPath));
                return;
            }

            var materialUri = geometry.OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault()?.Material.Uri;
            string? materialRef;
            try
            {
                materialRef = ResolveMaterialRef(materialUri);
            }
            catch (ArgumentException ex)
            {
                diagnostics.Add(CreateDiagnostic(
                    operationId,
                    DiagnosticSeverity.Error,
                    ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                    $"Scene node `{node.Name}` references material `{materialUri}` that cannot be normalized: {ex.Message}",
                    descriptorPath,
                    descriptorVirtualPath));
                return;
            }

            if (materialRef is not null)
            {
                _ = materialRefs.Add(materialRef);
            }

            renderables.Add(new NativeRenderable(nodeIndex, geometryRef, materialRef, node.IsVisible));
        }

        void AddEnvironmentDiagnostics()
        {
            if (scene.Environment.ExposureMode != ExposureMode.Auto)
            {
                diagnostics.Add(CreateUnsupportedFieldDiagnostic("Environment.ExposureMode"));
            }

            if (Math.Abs(scene.Environment.ExposureCompensation) > float.Epsilon)
            {
                diagnostics.Add(CreateUnsupportedFieldDiagnostic("Environment.ExposureCompensation"));
            }

            if (scene.Environment.ToneMapping != ToneMappingMode.Aces)
            {
                diagnostics.Add(CreateUnsupportedFieldDiagnostic("Environment.ToneMapping"));
            }

            if (scene.Environment.BackgroundColor != default)
            {
                diagnostics.Add(CreateUnsupportedFieldDiagnostic("Environment.BackgroundColor"));
            }
        }

        DiagnosticRecord CreateUnsupportedFieldDiagnostic(string field)
            => CreateDiagnostic(
                operationId,
                DiagnosticSeverity.Warning,
                ContentPipelineDiagnosticCodes.SceneUnsupportedField,
                $"Scene field `{field}` is not emitted by the ED-M07 descriptor slice.",
                descriptorPath,
                descriptorVirtualPath);
    }

    private static ContentCookInput FindSceneInput(ContentCookScope scope)
        => scope.Inputs.FirstOrDefault(static input => input.Kind == ContentCookAssetKind.Scene && input.Role == ContentCookInputRole.Primary)
           ?? scope.Inputs.First(static input => input.Kind == ContentCookAssetKind.Scene);

    private static string GetDerivedSceneDescriptorPath(ContentCookScope scope, ContentCookInput sceneInput)
    {
        var name = Path.GetFileName(sceneInput.SourceRelativePath);
        var normalized = ContentPipelinePaths.NormalizeSceneDescriptorName(name);
        return Path.Combine(scope.Project.ProjectRoot, ".pipeline", "Scenes", normalized + ".oscene.json");
    }

    private static IEnumerable<ContentCookInput> ResolveMaterialDependencies(Scene scene, ContentCookScope scope)
        => scene.AllNodes
            .SelectMany(static node => node.Components.OfType<GeometryComponent>())
            .Select(static geometry => geometry.OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault()?.Material.Uri)
            .Where(static uri => uri is not null && !IsEmptyAssetUri(uri))
            .Cast<Uri>()
            .Distinct()
            .Select(uri => TryResolveAuthoringInput(scope, uri, ContentCookAssetKind.Material, ".omat"))
            .Where(static input => input is not null)
            .Cast<ContentCookInput>();

    private static IEnumerable<ContentCookInput> ResolveGeometryDependencies(Scene scene, ContentCookScope scope)
        => scene.AllNodes
            .SelectMany(static node => node.Components.OfType<GeometryComponent>())
            .Select(static geometry => geometry.Geometry?.Uri)
            .Where(static uri => uri is not null
                                 && !IsEmptyAssetUri(uri)
                                 && !ProceduralGeometryDescriptorService.IsGeneratedBasicShape(uri))
            .Cast<Uri>()
            .Distinct()
            .Select(uri => TryResolveAuthoringInput(scope, uri, ContentCookAssetKind.Geometry, ".ogeo"))
            .Where(static input => input is not null)
            .Cast<ContentCookInput>();

    private static ContentCookInput? TryResolveAuthoringInput(
        ContentCookScope scope,
        Uri assetUri,
        ContentCookAssetKind kind,
        string nativeExtension)
    {
        if (!string.Equals(assetUri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        var path = Uri.UnescapeDataString(assetUri.AbsolutePath).TrimStart('/').Replace('\\', '/');
        var slash = path.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            return null;
        }

        var mountName = path[..slash];
        var mountRelative = path[(slash + 1)..];
        var mount = scope.Project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, mountName, StringComparison.OrdinalIgnoreCase));
        if (mount is null)
        {
            return null;
        }

        var sourceRelativePath = Path.Combine(mount.RelativePath, mountRelative).Replace('\\', '/');
        return new ContentCookInput(
            assetUri,
            kind,
            mountName,
            sourceRelativePath,
            Path.Combine(scope.Project.ProjectRoot, sourceRelativePath),
            ContentPipelinePaths.ToNativeDescriptorPath(assetUri, nativeExtension),
            ContentCookInputRole.Dependency);
    }

    private static string? ResolveGeometryRef(Uri geometryUri, IReadOnlyList<ContentCookInput> generatedGeometryInputs)
    {
        if (ProceduralGeometryDescriptorService.IsGeneratedBasicShape(geometryUri))
        {
            return generatedGeometryInputs
                .FirstOrDefault(input => input.AssetUri == geometryUri)
                ?.OutputVirtualPath;
        }

        return ContentPipelinePaths.ToNativeDescriptorPath(geometryUri, ".ogeo");
    }

    private static string? ResolveMaterialRef(Uri? materialUri)
    {
        if (materialUri is null || IsEmptyAssetUri(materialUri))
        {
            return null;
        }

        if (string.Equals(materialUri.ToString(), AssetUris.BuildGeneratedUri("Materials/Default"), StringComparison.OrdinalIgnoreCase))
        {
            return "/Engine/Generated/Materials/Default.omat";
        }

        return ContentPipelinePaths.ToNativeDescriptorPath(materialUri, ".omat");
    }

    private static bool IsEmptyAssetUri(Uri uri)
        => string.Equals(uri.ToString(), $"{AssetUris.Scheme}:///__uninitialized__", StringComparison.OrdinalIgnoreCase);

    private static NativeLightCommon ToCommon(LightComponent light)
        => new(
            light.AffectsWorld,
            ToArray(light.Color),
            light.CastsShadows,
            light.ExposureCompensation);

    private static bool IsSceneSunLight(Scene scene, SceneNode node, DirectionalLightComponent light)
        => scene.Environment.SunNodeId is { } sunNodeId
            ? light.IsSunLight && sunNodeId == node.Id
            : light.IsSunLight;

    private static NativeEnvironment CreateEnvironment(bool atmosphereEnabled)
        => new(CreateDefaultSkyAtmosphere(atmosphereEnabled));

    private static NativeSkyAtmosphereEnvironment CreateDefaultSkyAtmosphere(bool enabled)
        => new(
            Enabled: enabled,
            PlanetRadiusMeters: 6_360_000.0f,
            AtmosphereHeightMeters: 80_000.0f,
            GroundAlbedoRgb: [0.4f, 0.4f, 0.4f],
            RayleighScatteringRgb: [5.8e-6f, 13.5e-6f, 33.1e-6f],
            RayleighScaleHeightMeters: 8_000.0f,
            MieScatteringRgb: [21.0e-6f, 21.0e-6f, 21.0e-6f],
            MieAbsorptionRgb: [0.0f, 0.0f, 0.0f],
            MieScaleHeightMeters: 1_200.0f,
            MieAnisotropy: 0.8f,
            OzoneAbsorptionRgb: [0.65e-6f, 1.88e-6f, 0.085e-6f],
            OzoneDensityProfile: [25_000.0f, 15_000.0f, 0.0f],
            MultiScatteringFactor: 1.0f,
            SkyLuminanceFactorRgb: [1.0f, 1.0f, 1.0f],
            SkyAndAerialPerspectiveLuminanceFactorRgb: [1.0f, 1.0f, 1.0f],
            AerialPerspectiveDistanceScale: 1.0f,
            AerialScatteringStrength: 1.0f,
            AerialPerspectiveStartDepthMeters: 0.0f,
            HeightFogContribution: 1.0f,
            TraceSampleCountScale: 1.0f,
            TransmittanceMinLightElevationDegrees: -90.0f,
            SunDiskEnabled: true,
            Holdout: false,
            RenderInMainPass: true);

    private static float[] ToArray(Vector3 value) => [value.X, value.Y, value.Z];

    private static float[] ToArray(Quaternion value) => [value.X, value.Y, value.Z, value.W];

    private static DiagnosticRecord CreateDiagnostic(
        Guid operationId,
        DiagnosticSeverity severity,
        string code,
        string message,
        string descriptorPath,
        string descriptorVirtualPath)
        => new()
        {
            OperationId = operationId,
            Domain = FailureDomain.ContentPipeline,
            Severity = severity,
            Code = code,
            Message = message,
            AffectedPath = descriptorPath,
            AffectedVirtualPath = descriptorVirtualPath,
        };
}
