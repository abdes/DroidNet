// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

#pragma warning disable MA0051 // Scene descriptor generation is a single schema mapping operation.

/// <summary>
/// Generates native Oxygen scene descriptors from editor scene documents.
/// </summary>
/// <param name="proceduralGeometryDescriptors">The generated geometry descriptor service.</param>
public sealed partial class SceneDescriptorGenerator(IProceduralGeometryDescriptorService proceduralGeometryDescriptors) : ISceneDescriptorGenerator
{
    private const int NativeSceneDescriptorVersion = 10;
    private const double MaximumExposureLogLuminance = 32;
    private static readonly Lazy<EditorSchemaCatalog> SceneSchemas = new(() =>
        EditorSchemaCatalog.LoadFromDirectory(Path.Combine(
            Path.GetDirectoryName(typeof(EditorSchemaCatalog).Assembly.Location) ?? AppContext.BaseDirectory,
            "Schemas")));

    private readonly IProceduralGeometryDescriptorService proceduralGeometryDescriptors = proceduralGeometryDescriptors
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
        var aerialStart = scene.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters;
        var aerialValidation = SceneEnvironmentConstraints.ValidateAerialStart(aerialStart);
        if (!aerialValidation.IsValid)
        {
            diagnostics.Add(new DiagnosticRecord
            {
                OperationId = operationId,
                Domain = FailureDomain.ContentPipeline,
                Severity = DiagnosticSeverity.Error,
                Code = ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                Message = aerialValidation.Message,
                TechnicalMessage = string.Create(CultureInfo.InvariantCulture, $"Scene '{scene.Name}': environment.sky_atmosphere.aerial_perspective_start_depth_m = {aerialStart}."),
                AffectedPath = sceneInput.SourceAbsolutePath,
                AffectedVirtualPath = sceneInput.AssetUri.AbsolutePath,
                AffectedEntity = new AffectedScope
                {
                    ProjectId = scope.Project.ProjectId,
                    SceneId = scene.Id,
                    SceneName = scene.Name,
                    AssetVirtualPath = sceneInput.AssetUri.AbsolutePath,
                    ComponentName = "Environment",
                },
                SuggestedAction = new PrimaryAction
                {
                    ActionId = "Cook.GoToProperty",
                    Label = "Go to property",
                    Kind = PrimaryActionKind.Custom,
                    Payload = new Dictionary<string, string>(StringComparer.Ordinal)
                    {
                        ["AssetUri"] = sceneInput.AssetUri.AbsoluteUri,
                        ["PropertyPath"] = SceneEnvironmentConstraints.AerialStartPropertyPath,
                    },
                },
            });
            return new(sceneInput.AssetUri, descriptorPath, descriptorVirtualPath, Dependencies: [], diagnostics);
        }

        var lightIssue = LightValidation.ValidateScene(scene);
        if (lightIssue is not null)
        {
            diagnostics.Add(CreateDiagnostic(operationId, DiagnosticSeverity.Error,
                ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                lightIssue, descriptorPath, descriptorVirtualPath));
            return new(sceneInput.AssetUri, descriptorPath, descriptorVirtualPath, Dependencies: [], diagnostics);
        }

        var exposureIssue = ValidatePostProcess(scene.Environment.PostProcess);
        if (exposureIssue is not null)
        {
            diagnostics.Add(CreateDiagnostic(
                operationId,
                DiagnosticSeverity.Error,
                ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                exposureIssue,
                descriptorPath,
                descriptorVirtualPath));
            return new(sceneInput.AssetUri, descriptorPath, descriptorVirtualPath, Dependencies: [], diagnostics);
        }

        var generatedGeometryUris = scene.AllNodes
            .SelectMany(static node => node.Components.OfType<GeometryComponent>())
            .Select(static geometry => geometry.Geometry?.Uri)
            .Where(static uri => uri is not null && ProceduralGeometryDescriptorService.IsGeneratedBasicShape(uri))
            .Cast<Uri>()
            .Concat(scene.AllNodes.SelectMany(static node => node.Components.OfType<GeometryComponent>())
                .SelectMany(static geometry => geometry.OverrideSlots.OfType<MaterialsSlot>())
                .Select(static slot => slot.Material.Uri)
                .Where(IsDefaultMaterial)
                .Select(static _ => AssetUris.BuildGeneratedUri("Materials/Default")))
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
        if (scene.Environment.PostProcess.AutoExposureMeteringMask is { } maskUri)
        {
            var maskInput = CookInputResolver.IsAuthoringUri(scope.Project, maskUri)
                ? CookInputResolver.Resolve(scope.Project, maskUri, ContentCookInputRole.Dependency) : null;
            if (maskInput is null || maskInput.Kind != ContentCookAssetKind.Texture
                || !string.Equals(maskInput.MountName, sceneInput.MountName, StringComparison.Ordinal))
            {
                diagnostics.Add(CreateDiagnostic(
                    operationId,
                    DiagnosticSeverity.Error,
                    ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                    "The exposure metering mask must be a texture descriptor in the scene's own content mount.",
                    descriptorPath,
                    descriptorVirtualPath));
                return new(sceneInput.AssetUri, descriptorPath, descriptorVirtualPath, Dependencies: [], diagnostics);
            }
        }

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
            Schema: string.Create(CultureInfo.InvariantCulture, $"oxygen.scene-descriptor.v{NativeSceneDescriptorVersion}"),
            Version: NativeSceneDescriptorVersion,
            Name: ContentPipelinePaths.NormalizeSceneDescriptorName(Path.GetFileName(sceneInput.SourceRelativePath)),
            Nodes: nodes,
            Renderables: renderables.Count == 0 ? null : renderables,
            Cameras: cameras.Count == 0 ? null : new NativeCameras(cameras),
            Lights: lights,
            Environment: CreateEnvironment(scene.Environment),
            References: materialRefs.Count == 0 ? null : new NativeReferences(materialRefs.ToArray(), ExtraAssets: null));

        JsonNode? descriptorJson;
        try
        {
            descriptorJson = JsonSerializer.SerializeToNode(descriptor, SceneDescriptorJson.Options);
        }
        catch (ArgumentException error)
        {
            diagnostics.Add(CreateDiagnostic(
                operationId,
                DiagnosticSeverity.Error,
                ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                $"Scene descriptor contains an unrepresentable value: {error.Message}",
                descriptorPath,
                descriptorVirtualPath));
            return new(sceneInput.AssetUri, descriptorPath, descriptorVirtualPath, dependencyInputs, diagnostics);
        }

        if (!SceneSchemas.Value.ValidateAgainstEngine("oxygen.scene-descriptor.schema.json", descriptorJson))
        {
            diagnostics.Add(CreateDiagnostic(
                operationId,
                DiagnosticSeverity.Error,
                ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                "Scene descriptor values do not satisfy the current scene schema. Review camera and environment values.",
                descriptorPath,
                descriptorVirtualPath));
            return new(sceneInput.AssetUri, descriptorPath, descriptorVirtualPath, dependencyInputs, diagnostics);
        }

        Directory.CreateDirectory(Path.GetDirectoryName(descriptorPath)!);
        var stream = File.Create(descriptorPath);
        await using (stream.ConfigureAwait(false))
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

            // Boolean authoring fields are explicit local choices, including on children.
            nodes.Add(new NativeSceneNode(
                node.Name,
                parentIndex,
                new NativeNodeFlags(
                    node.IsVisible ? "shown" : "hidden",
                    node.IsStatic,
                    node.CastsShadows ? "on" : "off",
                    node.ReceivesShadows ? "on" : "off",
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
                    camera.FieldOfView * (MathF.PI / 180f),
                    camera.AspectRatio,
                    camera.AspectMode == Oxygen.Managed.Core.CameraAspectMode.Auto ? "auto" : "fixed",
                    camera.NearPlane,
                    camera.FarPlane,
                    camera.ApertureF,
                    camera.ShutterRate,
                    camera.Iso));
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
                    (int)light.AtmosphereSlot,
                    light.UsePerPixelAtmosphereTransmittance,
                    ToArray(light.AtmosphereDiskLuminanceScaleRgb),
                    light.CascadeCount,
                    (int)light.SplitMode,
                    light.MaxShadowDistance,
                    [light.CascadeDistances.X, light.CascadeDistances.Y, light.CascadeDistances.Z, light.CascadeDistances.W],
                    light.DistributionExponent,
                    light.TransitionFraction,
                    light.DistanceFadeoutFraction));
            }

            foreach (var light in node.Components.OfType<PointLightComponent>())
            {
                pointLights.Add(new NativePointLight(
                    nodeIndex,
                    ToCommon(light),
                    light.LuminousFluxLumens,
                    light.Range,
                    light.SourceRadius));
            }

            foreach (var light in node.Components.OfType<SpotLightComponent>())
            {
                spotLights.Add(new NativeSpotLight(
                    nodeIndex,
                    ToCommon(light),
                    light.LuminousFluxLumens,
                    light.Range,
                    light.SourceRadius,
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

            var overrides = new List<NativeMaterialSlotOverride>();
            var assignedSlots = new HashSet<Guid>();
            foreach (var slot in geometry.OverrideSlots.OfType<MaterialsSlot>())
            {
                if (slot.Target.GeometryUri != geometry.Geometry.Uri || slot.Target.SlotId == Guid.Empty
                    || !assignedSlots.Add(slot.Target.SlotId))
                {
                    diagnostics.Add(CreateDiagnostic(operationId, DiagnosticSeverity.Error,
                        ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                        $"Scene node '{node.Name}' has an invalid or unresolved material-slot assignment.",
                        descriptorPath, descriptorVirtualPath));
                    return;
                }

                string? materialRef;
                try
                {
                    materialRef = ResolveMaterialRef(slot.Material.Uri, generatedGeometryInputs);
                }
                catch (ArgumentException exception)
                {
                    diagnostics.Add(CreateDiagnostic(operationId, DiagnosticSeverity.Error,
                        ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                        $"Scene node '{node.Name}' references invalid material '{slot.Material.Uri}': {exception.Message}",
                        descriptorPath, descriptorVirtualPath));
                    return;
                }

                if (materialRef is null)
                {
                    diagnostics.Add(CreateDiagnostic(operationId, DiagnosticSeverity.Error,
                        ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                        $"Scene node '{node.Name}' has an empty material assignment. Clear removes the slot override.",
                        descriptorPath, descriptorVirtualPath));
                    return;
                }

                _ = materialRefs.Add(materialRef);
                overrides.Add(new(slot.Target.SlotId, materialRef, slot.Target.LayoutRevision));
            }

            renderables.Add(new NativeRenderable(nodeIndex, geometryRef, overrides, node.IsVisible));
        }
    }

    private static ContentCookInput FindSceneInput(ContentCookScope scope)
        => scope.Inputs.FirstOrDefault(static input => input.Kind == ContentCookAssetKind.Scene && input.Role == ContentCookInputRole.Primary)
           ?? scope.Inputs.First(static input => input.Kind == ContentCookAssetKind.Scene);

    private static string GetDerivedSceneDescriptorPath(ContentCookScope scope, ContentCookInput sceneInput)
    {
        var name = Path.GetFileName(sceneInput.SourceRelativePath);
        var normalized = ContentPipelinePaths.NormalizeSceneDescriptorName(name);
        var folder = Path.GetDirectoryName(sceneInput.SourceRelativePath) ?? string.Empty;
        return Path.Combine(scope.PreparationRoot ?? scope.InputRoot, ".pipeline", "Scenes", folder, normalized + ".oscene.json");
    }

    private static string? ResolveGeometryRef(Uri geometryUri, IReadOnlyList<ContentCookInput> generatedGeometryInputs)
        => ProceduralGeometryDescriptorService.IsGeneratedBasicShape(geometryUri)
            ? generatedGeometryInputs.FirstOrDefault(input => input.AssetUri == geometryUri)?.OutputVirtualPath
            : ContentPipelinePaths.ToNativeDescriptorPath(geometryUri, ".ogeo");

    private static string? ResolveMaterialRef(Uri? materialUri, IReadOnlyList<ContentCookInput> generatedInputs)
        => materialUri is null || IsEmptyAssetUri(materialUri)
            ? null : IsDefaultMaterial(materialUri)
            ? generatedInputs.FirstOrDefault(static input => IsDefaultMaterial(input.AssetUri))?.OutputVirtualPath
            : ContentPipelinePaths.ToNativeDescriptorPath(materialUri, ".omat");

    private static bool IsDefaultMaterial(Uri? uri)
        => uri is not null && string.Equals(
            uri.AbsoluteUri,
            AssetUris.BuildGeneratedUri("Materials/Default").AbsoluteUri, StringComparison.OrdinalIgnoreCase);

    private static bool IsEmptyAssetUri(Uri uri)
        => string.Equals(uri.ToString(), $"{AssetUris.Scheme}:///__uninitialized__", StringComparison.OrdinalIgnoreCase);

    private static NativeLightCommon ToCommon(LightComponent light)
        => new(
            light.AffectsWorld,
            ToArray(light.Color),
            light.CastsShadows,
            light.ExposureCompensation,
            new NativeLightShadow(light.ShadowBias, light.ShadowNormalBias,
                light.ContactShadows, (int)light.ShadowResolutionHint));

    private static string? ValidateExposureRelationships(PostProcessEnvironmentData exposure)
    {
        if (exposure.AutoExposureMinEv > exposure.AutoExposureMaxEv)
        {
            return "Auto exposure minimum EV must not exceed maximum EV.";
        }

        if (exposure.AutoExposureLowPercentile >= exposure.AutoExposureHighPercentile)
        {
            return "Auto exposure low percentile must be below the high percentile.";
        }

        if (exposure.AutoExposureMinLogLuminance + (double)exposure.AutoExposureLogLuminanceRange > MaximumExposureLogLuminance)
        {
            return "Auto exposure histogram upper luminance must not exceed 32 EV.";
        }

        if (exposure.AutoExposureCompensationCurve.IsDefault)
        {
            return "Exposure compensation curve must be initialized; use an empty array for no curve.";
        }

        for (var index = 1; index < exposure.AutoExposureCompensationCurve.Length; index++)
        {
            if (exposure.AutoExposureCompensationCurve[index].MeteredEv <= exposure.AutoExposureCompensationCurve[index - 1].MeteredEv)
            {
                return "Exposure compensation curve EV keys must be strictly increasing.";
            }
        }

        if (exposure.AutoExposureMeteringMask is { } mask)
        {
            if (!mask.IsAbsoluteUri)
            {
                return "Exposure metering mask must use an absolute asset URI.";
            }

            try
            {
                _ = ContentPipelinePaths.ToNativeDescriptorPath(mask, ".otex");
            }
            catch (ArgumentException error)
            {
                return $"Invalid exposure metering mask: {error.Message}";
            }
        }

        return null;
    }

    // Match the captured lighting supplied by Interop's ApplySkyLight.
    private static NativeEnvironment CreateEnvironment(SceneEnvironmentData environment)
        => new(
            CreateSkyAtmosphere(environment.AtmosphereEnabled, environment.SkyAtmosphere ?? new()),
            new NativeSkyLightEnvironment(
                Enabled: true,
                Source: 0,
                Intensity: 1.0f,
                TintRgb: [1.0f, 1.0f, 1.0f],
                DiffuseIntensity: 1.0f,
                SpecularIntensity: 1.0f,
                SourceCubemapAngleRadians: 0.0f,
                LowerHemisphereColor: [0.02f, 0.02f, 0.03f],
                LowerHemisphereIsSolidColor: true,
                LowerHemisphereBlendAlpha: 1.0f,
                VolumetricScatteringIntensity: 1.0f,
                AffectReflections: true),
            CreatePostProcess(environment.PostProcess ?? new()),
            new NativeBackgroundEnvironment(Enabled: true, ToArray(environment.BackgroundColor)));

    private static NativePostProcessEnvironment CreatePostProcess(PostProcessEnvironmentData authored)
        => new(
            Enabled: true,
            ToneMapper: (int)authored.ToneMapper,
            ExposureMode: (int)authored.ExposureMode,
            ExposureEnabled: authored.ExposureEnabled,
            ExposureCompensationEv: authored.ExposureCompensationEv,
            ExposureKey: authored.ExposureKey,
            ManualExposureEv: authored.ManualExposureEv,
            AutoExposureMinEv: authored.AutoExposureMinEv,
            AutoExposureMaxEv: authored.AutoExposureMaxEv,
            AutoExposureSpeedUp: authored.AutoExposureSpeedUp,
            AutoExposureSpeedDown: authored.AutoExposureSpeedDown,
            AutoExposureMeteringMode: (int)authored.AutoExposureMeteringMode,
            AutoExposureLowPercentile: authored.AutoExposureLowPercentile,
            AutoExposureHighPercentile: authored.AutoExposureHighPercentile,
            AutoExposureMinLogLuminance: authored.AutoExposureMinLogLuminance,
            AutoExposureLogLuminanceRange: authored.AutoExposureLogLuminanceRange,
            AutoExposureTargetLuminance: authored.AutoExposureTargetLuminance,
            AutoExposureSpotMeterRadius: authored.AutoExposureSpotMeterRadius,
            AutoExposureBlackInfluence: authored.AutoExposureBlackInfluence,
            AutoExposureTransitionDistanceEv: authored.AutoExposureTransitionDistanceEv,
            AutoExposureMeteringMask: GetMeteringMaskPath(authored.AutoExposureMeteringMask),
            AutoExposureCompensationCurve: authored.AutoExposureCompensationCurve
                .Select(static key => new NativeExposureCompensationKey(key.MeteredEv, key.CompensationEv)).ToArray(),
            BloomIntensity: authored.BloomIntensity,
            BloomThreshold: authored.BloomThreshold,
            Saturation: authored.Saturation,
            Contrast: authored.Contrast,
            VignetteIntensity: authored.VignetteIntensity,
            DisplayGamma: authored.DisplayGamma);

    private static string? GetMeteringMaskPath(Uri? mask)
        => mask is null ? null : ContentPipelinePaths.ToNativeDescriptorPath(mask, ".otex");

    // Mirrors the native Earth baseline in Oxygen/Core/Types/Atmosphere.h.
    private static NativeSkyAtmosphereEnvironment CreateSkyAtmosphere(bool enabled, SkyAtmosphereEnvironmentData authored)
        => new(
            Enabled: enabled,
            PlanetRadiusMeters: authored.PlanetRadiusMeters,
            AtmosphereHeightMeters: authored.AtmosphereHeightMeters,
            GroundAlbedoRgb: ToArray(authored.GroundAlbedoRgb),
            RayleighScatteringRgb: [5.802e-6f, 13.558e-6f, 33.1e-6f],
            RayleighScaleHeightMeters: authored.RayleighScaleHeightMeters,
            MieScatteringRgb: [3.996e-6f, 3.996e-6f, 3.996e-6f],
            MieAbsorptionRgb: [4.405e-7f, 4.405e-7f, 4.405e-7f],
            MieScaleHeightMeters: authored.MieScaleHeightMeters,
            MieAnisotropy: authored.MieAnisotropy,
            OzoneAbsorptionRgb: [0.650e-6f, 1.881e-6f, 0.085e-6f],
            OzoneDensityProfile: [25_000.0f, 15_000.0f, 0.0f],
            MultiScatteringFactor: 1.0f,
            SkyLuminanceFactorRgb: ToArray(authored.SkyLuminanceFactorRgb),
            SkyAndAerialPerspectiveLuminanceFactorRgb: ToArray(authored.SkyLuminanceFactorRgb),
            AerialPerspectiveDistanceScale: authored.AerialPerspectiveDistanceScale,
            AerialScatteringStrength: authored.AerialScatteringStrength,
            AerialPerspectiveStartDepthMeters: authored.AerialPerspectiveStartDepthMeters,
            HeightFogContribution: authored.HeightFogContribution,
            TraceSampleCountScale: 1.0f,
            TransmittanceMinLightElevationDegrees: -90.0f,
            SunDiskEnabled: authored.SunDiskEnabled,
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
