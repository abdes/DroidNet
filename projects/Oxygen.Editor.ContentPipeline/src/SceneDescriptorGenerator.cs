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
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

#pragma warning disable MA0051 // Scene descriptor generation is a single schema mapping operation.

/// <summary>
/// Generates native Oxygen scene descriptors from editor scene documents.
/// </summary>
public sealed partial class SceneDescriptorGenerator : ISceneDescriptorGenerator
{
    private const double MaximumExposureLogLuminance = 32;
    private static readonly Lazy<EditorSchemaCatalog> SceneSchemas = new(() =>
        EditorSchemaCatalog.LoadFromDirectory(Path.Combine(
            Path.GetDirectoryName(typeof(EditorSchemaCatalog).Assembly.Location) ?? AppContext.BaseDirectory,
            "Schemas")));

    // The engine schema owns the descriptor version; a descriptor names the one it requires.
    private static readonly Lazy<int> NativeSceneDescriptorVersion = new(() =>
        SceneSchemas.Value.GetEngineSchema("oxygen.scene-descriptor.schema.json")["properties"]!["version"]!["const"]!
            .GetValue<int>());

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
        var descriptorVirtualPath = ContentPipelinePaths.ToCookedScenePath(sceneInput.AssetUri);
        var diagnostics = new List<DiagnosticRecord>();
        var operationId = Guid.NewGuid();
        var aerialStart = scene.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters;
        var aerialValidation = SceneEnvironmentConstraints.ValidateAerialStart(aerialStart);
        var site = new PropertySite(operationId, scope, sceneInput, scene, diagnostics);
        if (!aerialValidation.IsValid)
        {
            site.Report(
                SceneEnvironmentConstraints.AerialStartPropertyPath,
                aerialValidation.Message,
                string.Create(CultureInfo.InvariantCulture, $"Scene '{scene.Name}': environment.sky_atmosphere.aerial_perspective_start_depth_m = {aerialStart}."));
            return new(sceneInput.AssetUri, descriptorPath, descriptorVirtualPath, Dependencies: [], diagnostics);
        }

        var lightIssue = LightValidation.ValidateScene(scene);
        if (lightIssue is not null)
        {
            diagnostics.Add(CreateDiagnostic(
                operationId,
                DiagnosticSeverity.Error,
                ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                lightIssue,
                descriptorPath,
                descriptorVirtualPath));
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

        var authoredReferences = CreateNativeReferences(scene.References, out var referenceIssue);
        if (referenceIssue is not null)
        {
            diagnostics.Add(CreateDiagnostic(
                operationId,
                DiagnosticSeverity.Error,
                ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                referenceIssue,
                descriptorPath,
                descriptorVirtualPath));
            return new(sceneInput.AssetUri, descriptorPath, descriptorVirtualPath, Dependencies: [], diagnostics);
        }

        var nodes = new List<NativeSceneNode>();
        var renderables = new List<NativeRenderable>();
        var nodeSites = new List<DescriptorNodeSite>();
        var renderableNodes = new List<int>();
        var cameras = new List<NativePerspectiveCamera>();
        var orthographicCameras = new List<NativeOrthographicCamera>();
        var directionalLights = new List<NativeDirectionalLight>();
        var pointLights = new List<NativePointLight>();
        var spotLights = new List<NativeSpotLight>();
        var localFogVolumes = new List<NativeLocalFogVolume>();
        var materialRefs = new SortedSet<string>(StringComparer.Ordinal);
        var dependencyInputs = new List<ContentCookInput>();
        foreach (var (textureUri, label) in EnvironmentTextures(scene.Environment))
        {
            var textureInput = CookInputResolver.IsAuthoringUri(scope.Project, textureUri)
                ? CookInputResolver.Resolve(scope.Project, textureUri, ContentCookInputRole.Dependency) : null;
            if (textureInput is null || textureInput.Kind != ContentCookAssetKind.Texture
                || !string.Equals(textureInput.MountName, sceneInput.MountName, StringComparison.Ordinal))
            {
                diagnostics.Add(CreateDiagnostic(
                    operationId,
                    DiagnosticSeverity.Error,
                    ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                    $"The {label} must be a texture descriptor in the scene's own content mount.",
                    descriptorPath,
                    descriptorVirtualPath));
                return new(sceneInput.AssetUri, descriptorPath, descriptorVirtualPath, Dependencies: [], diagnostics);
            }
        }

        var nodeIndices = new Dictionary<NodeId, int>();
        foreach (var root in scene.RootNodes)
        {
            AddNode(root, parentIndex: null);
        }

        var scripts = CreateScriptBindings(scene, scope, sceneInput, nodeIndices, operationId, diagnostics);
        var startingCamera = CreateStartingCamera(scene, nodeIndices, site);
        var startingInputContexts = CreateStartingInputContexts(scene, scope, site);
        if (diagnostics.Exists(static issue => issue.Severity >= DiagnosticSeverity.Error))
        {
            return new(sceneInput.AssetUri, descriptorPath, descriptorVirtualPath, Dependencies: [], diagnostics);
        }

        var lights = directionalLights.Count == 0 && pointLights.Count == 0 && spotLights.Count == 0
            ? null
            : new NativeLights(
                Directional: directionalLights.Count == 0 ? null : directionalLights,
                Point: pointLights.Count == 0 ? null : pointLights,
                Spot: spotLights.Count == 0 ? null : spotLights);
        var references = materialRefs.Count == 0 && authoredReferences is null
            ? null
            : new NativeReferences(
                Materials: materialRefs.Count == 0 ? null : materialRefs.ToArray(),
                Scripts: authoredReferences?.Scripts,
                InputActions: authoredReferences?.InputActions,
                PhysicsSidecars: authoredReferences?.PhysicsSidecars,
                ExtraAssets: authoredReferences?.ExtraAssets);
        var descriptor = new NativeSceneDescriptor(
            Schema: string.Create(CultureInfo.InvariantCulture, $"oxygen.scene-descriptor.v{NativeSceneDescriptorVersion.Value}"),
            Version: NativeSceneDescriptorVersion.Value,
            Name: Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(sceneInput.SourceRelativePath)),
            Nodes: nodes,
            StartingCamera: startingCamera,
            StartingInputContexts: startingInputContexts,
            Renderables: renderables.Count == 0 ? null : renderables,
            Cameras: CreateCameras(cameras, orthographicCameras),
            Lights: lights,
            Environment: CreateEnvironment(scene.Environment),
            LocalFogVolumes: localFogVolumes.Count == 0 ? null : localFogVolumes,
            References: references);

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

        _ = Directory.CreateDirectory(Path.GetDirectoryName(descriptorPath)!);
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
            diagnostics)
        {
            SceneId = scene.Id,
            SceneName = scene.Name,
            ScriptBindings = scripts.Bindings,
            ScriptInstanceBindings = scripts.InstanceBindings,
            ScriptInstanceBindingSites = scripts.InstanceSites,
            ScriptBindingSites = scripts.Sites,
            ScriptReferences = scripts.References,
            Nodes = nodeSites,
            RenderableNodes = renderableNodes,
        };

        void AddNode(SceneNode node, int? parentIndex)
        {
            var nodeIndex = nodes.Count;
            nodeIndices[node.Id] = nodeIndex;
            var transform = node.Transform;
            var flags = node.Flags;

            // Every source mode is written, so the cooker's omitted-value defaults never decide one.
            nodes.Add(new NativeSceneNode(
                node.Id.ToString(),
                node.Metadata.Name,
                parentIndex,
                new NativeNodeFlags(
                    ToDescriptor(flags.Visibility),
                    flags.IsStatic,
                    ToDescriptor(flags.CastsShadows),
                    ToDescriptor(flags.ReceivesShadows),
                    flags.IsRayCastingSelectable,
                    flags.IgnoreParentTransform),
                new NativeNodeTransform(
                    ToArray(transform.LocalPosition),
                    ToArray(transform.LocalRotation),
                    ToArray(transform.LocalScale)),
                node.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } instance ? CreateInstance(node, instance) : null));

            // In the order CreateInstance writes the changes, which the cooker's diagnostics index.
            nodeSites.Add(new DescriptorNodeSite(node.Id, node.Metadata.Name)
            {
                InstanceChanges = node.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } changed
                    ? [.. changed.Changes.Ordered.Where(static entry => !entry.Value.Components.IsEmpty).Select(static entry => new DescriptorInstanceChange(entry.Key, entry.Value.Names))]
                    : [],
            });

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
                orthographicCameras.Add(new NativeOrthographicCamera(
                    nodeIndex,
                    camera.AspectMode == Oxygen.Managed.Core.CameraAspectMode.Auto ? "auto" : "fixed",
                    camera.OrthographicSize,
                    camera.AspectRatio,
                    camera.NearPlane,
                    camera.FarPlane,
                    camera.ApertureF,
                    camera.ShutterRate,
                    camera.Iso));
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

            foreach (var volume in node.Components.OfType<LocalFogVolumeComponent>())
            {
                localFogVolumes.Add(new NativeLocalFogVolume(
                    nodeIndex,
                    volume.Enabled,
                    volume.RadialFogExtinction,
                    volume.HeightFogExtinction,
                    volume.HeightFogFalloff,
                    volume.HeightFogOffset,
                    volume.FogPhaseG,
                    ToArray(volume.FogAlbedo),
                    ToArray(volume.FogEmissive),
                    volume.SortPriority));
            }

            foreach (var child in node.Hierarchy.Children)
            {
                AddNode(child, nodeIndex);
            }
        }

        void AddRenderable(SceneNode node, int nodeIndex, GeometryComponent geometry)
        {
            string geometryRef;
            try
            {
                geometryRef = ContentPipelinePaths.ToNativeDescriptorPath(geometry.Geometry, AssetFileTypes.Geometry);
            }
            catch (ArgumentException ex)
            {
                diagnostics.Add(CreateDiagnostic(
                    operationId,
                    DiagnosticSeverity.Error,
                    ContentPipelineDiagnosticCodes.GeometryDescriptorGenerationFailed,
                    $"Scene node `{node.Metadata.Name}` references geometry `{geometry.Geometry}` that cannot be normalized: {ex.Message}",
                    descriptorPath,
                    descriptorVirtualPath));
                return;
            }

            // The cooker refuses an override of a surface the geometry does not have.
            if (!TryWriteOverrides(node, geometry.Overrides, out var overrides))
            {
                return;
            }

            renderables.Add(new NativeRenderable(nodeIndex, geometryRef, overrides));
            renderableNodes.Add(nodeIndex);
        }

        // An instance root writes the scene its source instances as, and every change by node path and component kind.
        NativeSceneInstance? CreateInstance(SceneNode node, SceneInstanceComponent instance)
        {
            string sceneRef;
            try
            {
                sceneRef = AssetFileTypes.SceneProductOf(instance.Source) is { } scene
                    ? ContentPipelinePaths.ToCookedScenePath(scene)
                    : throw new ArgumentException($"'{instance.Source}' is neither a model nor a scene.", nameof(instance));
            }
            catch (ArgumentException exception)
            {
                diagnostics.Add(CreateDiagnostic(
                    operationId,
                    DiagnosticSeverity.Error,
                    ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                    $"Scene node '{node.Metadata.Name}' instances '{instance.Source}', which cannot be instanced: {exception.Message}",
                    descriptorPath,
                    descriptorVirtualPath));
                return null;
            }

            // Script parameter changes cook in the script sidecar.
            var changes = new List<NativeInstanceOverride>();
            foreach (var (path, nodeChanges) in instance.Changes.Ordered.Where(static entry => !entry.Value.Components.IsEmpty))
            {
                var components = new SortedDictionary<string, NativeOverrides>(StringComparer.Ordinal);
                foreach (var (kind, overrides) in nodeChanges.Components)
                {
                    if (!TryWriteOverrides(node, overrides, out var written))
                    {
                        return null;
                    }

                    if (written is not null)
                    {
                        components.Add(kind, written);
                    }
                }

                changes.Add(new NativeInstanceOverride([.. path.Nodes.Select(static id => id.ToString())], components));
            }

            return new NativeSceneInstance(sceneRef, changes.Count == 0 ? null : changes);
        }

        // Writes a component's overrides or an instanced node's changes by scene schema key: the component's own
        // values, then each surface's. A value that cannot be written is reported and nothing is written.
        bool TryWriteOverrides(SceneNode node, SourceOverrides source, out NativeOverrides? written)
        {
            written = null;
            var values = new SortedDictionary<string, JsonNode>(StringComparer.Ordinal);
            var parts = new List<NativeOverridePart>();
            foreach (var part in source.All.GroupBy(static entry => entry.Part))
            {
                var partValues = part.Key is null ? values : new SortedDictionary<string, JsonNode>(StringComparer.Ordinal);
                foreach (var entry in part)
                {
                    if (DescriptorValue(node, entry) is not { } value)
                    {
                        return false;
                    }

                    partValues.Add(entry.Property.JsonPointer, value);
                }

                if (part.Key is { } surface)
                {
                    parts.Add(new NativeOverridePart(surface.Kind, surface.Id, part.First().LayoutRevision!, partValues));
                }
            }

            written = values.Count == 0 && parts.Count == 0
                ? null
                : new NativeOverrides(values.Count == 0 ? null : values, parts.Count == 0 ? null : parts);
            return true;
        }

        // Each value is spelled once per value type of the schema, in the schema's units; an enum is its member's wire
        // value; materials are the only asset values.
        JsonNode? DescriptorValue(SceneNode node, SourceOverride entry)
        {
            switch (entry.Value)
            {
                case bool flag:
                    return JsonValue.Create(flag);
                case int number:
                    return JsonValue.Create(number);
                case float number:
                    return JsonValue.Create(number);
                case Vector3 vector:
                    return new JsonArray(vector.X, vector.Y, vector.Z);
                case Enum member:
                    return JsonValue.Create(Convert.ToInt32(member, CultureInfo.InvariantCulture));
                case LodPolicy policy:
                    return JsonSerializer.SerializeToNode(policy, SceneDescriptorJson.Options);
                case AssetPath material when entry.Property == SceneSchema.Geometry.Surface.Material.Id:
                    try
                    {
                        var materialRef = ContentPipelinePaths.ToNativeDescriptorPath(material, AssetFileTypes.Material);
                        _ = materialRefs.Add(materialRef);
                        return JsonValue.Create(materialRef);
                    }
                    catch (ArgumentException exception)
                    {
                        diagnostics.Add(CreateDiagnostic(
                            operationId,
                            DiagnosticSeverity.Error,
                            ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                            $"Scene node '{node.Metadata.Name}' references invalid material '{material}': {exception.Message}",
                            descriptorPath,
                            descriptorVirtualPath));
                        return null;
                    }

                default:
                    throw new InvalidOperationException($"Override '{entry.Property}' holds a {entry.Value.GetType().Name}, which has no descriptor spelling.");
            }
        }
    }

    private static ContentCookInput FindSceneInput(ContentCookScope scope)
        => scope.Inputs.FirstOrDefault(static input => input.Kind == ContentCookAssetKind.Scene && input.Role == ContentCookInputRole.Primary)
           ?? scope.Inputs.First(static input => input.Kind == ContentCookAssetKind.Scene);

    private static string GetDerivedSceneDescriptorPath(ContentCookScope scope, ContentCookInput sceneInput)
    {
        var folder = Path.GetDirectoryName(sceneInput.SourceRelativePath) ?? string.Empty;
        return Path.Combine(scope.PreparationRoot ?? scope.InputRoot, ".pipeline", "Scenes", folder, Path.GetFileName(sceneInput.SourceRelativePath));
    }

    private static NativeReferences? CreateNativeReferences(
        Oxygen.Editor.World.Serialization.SceneReferencesData references,
        out string? error)
    {
        ArgumentNullException.ThrowIfNull(references);

        try
        {
            var scripts = ResolveTypedReferences(references.Scripts, AssetFileTypes.Script);
            var inputActions = ResolveTypedReferences(references.InputActions, AssetFileTypes.InputAction);
            var physicsSidecars = ResolveTypedReferences(references.PhysicsSidecars, AssetFileTypes.PhysicsScene);
            if (references.ExtraAssets.Any(static asset => asset.IsMount))
            {
                throw new InvalidDataException("Extra scene asset references must name assets below a mount.");
            }

            var extraAssets = new SortedSet<string>(references.ExtraAssets.Select(static asset => asset.VirtualPath), StringComparer.Ordinal).ToArray();
            error = null;

            return scripts.Length == 0 && inputActions.Length == 0
                && physicsSidecars.Length == 0 && extraAssets.Length == 0
                ? null
                : new NativeReferences(
                    Materials: null,
                    Scripts: scripts.Length == 0 ? null : scripts,
                    InputActions: inputActions.Length == 0 ? null : inputActions,
                    PhysicsSidecars: physicsSidecars.Length == 0 ? null : physicsSidecars,
                    ExtraAssets: extraAssets.Length == 0 ? null : extraAssets);
        }
        catch (ArgumentException exception)
        {
            error = exception.Message;
            return null;
        }
        catch (InvalidDataException exception)
        {
            error = exception.Message;
            return null;
        }
    }

    private static string[] ResolveTypedReferences(
        IEnumerable<AssetPath> assetUris,
        string expectedExtension)
    {
        var paths = new SortedSet<string>(StringComparer.Ordinal);
        foreach (var assetUri in assetUris)
        {
            var nativePath = ContentPipelinePaths.ToNativeDescriptorPath(assetUri, expectedExtension);
            _ = paths.Add(nativePath);
        }

        return [.. paths];
    }

    private static NativeCameras? CreateCameras(
        List<NativePerspectiveCamera> perspective,
        List<NativeOrthographicCamera> orthographic)
        => perspective.Count == 0 && orthographic.Count == 0
            ? null
            : new NativeCameras(perspective.Count == 0 ? null : perspective, orthographic.Count == 0 ? null : orthographic);

    private static NativeLightCommon ToCommon(LightComponent light)
        => new(
            light.AffectsWorld,
            ToArray(light.Color),
            light.CastsShadows,
            light.ExposureCompensation,
            new NativeLightShadow(
                light.ShadowBias,
                light.ShadowNormalBias,
                light.ContactShadows,
                (int)light.ShadowResolutionHint));

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
            try
            {
                _ = ContentPipelinePaths.ToNativeDescriptorPath(mask, AssetFileTypes.Texture);
            }
            catch (ArgumentException error)
            {
                return $"Invalid exposure metering mask: {error.Message}";
            }
        }

        return null;
    }

    private static NativeEnvironment CreateEnvironment(SceneEnvironmentData environment)
        => new(
            CreateSkyAtmosphere(environment.AtmosphereEnabled, environment.SkyAtmosphere ?? new()),
            CreateSkyLight(environment.SkyLight ?? new()),
            CreateSkySphere(environment.SkySphere ?? new()),
            CreateFog(environment.Fog ?? new()),
            CreatePostProcess(environment.PostProcess ?? new()),
            new NativeBackgroundEnvironment(environment.Background.Enabled, ToArray(environment.Background.ColorRgb)));

    // The textures the cooked environment references: each must be cooked into
    // the scene's own source, as the native scene record binds it there. A
    // cubemap is referenced only while an enabled system's source uses it, so a
    // stale reference behind another source or a disabled system does not block
    // the cook.
    private static IEnumerable<(AssetPath Texture, string Label)> EnvironmentTextures(SceneEnvironmentData environment)
    {
        if (environment.PostProcess.AutoExposureMeteringMask is { } mask)
        {
            yield return (mask, "exposure metering mask");
        }

        if (environment.SkySphere.ActiveCubemap is { } backdrop)
        {
            yield return (backdrop, "sky sphere cubemap");
        }

        if (environment.SkyLight.ActiveCubemap is { } light)
        {
            yield return (light, "sky light cubemap");
        }
    }

    private static NativeSkyLightEnvironment CreateSkyLight(SkyLightEnvironmentData authored)
        => new(
            Enabled: authored.Enabled,
            Source: (int)authored.Source,
            CubemapRef: GetTexturePath(authored.ActiveCubemap),
            IlluminanceLux: authored.CubemapIlluminanceLux,
            Intensity: authored.Intensity,
            TintRgb: ToArray(authored.TintRgb),
            DiffuseIntensity: authored.DiffuseIntensity,
            SpecularIntensity: authored.SpecularIntensity,
            SourceCubemapAngleRadians: authored.CubemapAngleRadians,
            LowerHemisphereColor: ToArray(authored.LowerHemisphereColor),
            LowerHemisphereIsSolidColor: authored.LowerHemisphereIsSolidColor,
            LowerHemisphereBlendAlpha: authored.LowerHemisphereBlendAlpha,
            VolumetricScatteringIntensity: authored.VolumetricScatteringIntensity,
            AffectReflections: authored.AffectReflections);

    private static NativeSkySphereEnvironment CreateSkySphere(SkySphereEnvironmentData authored)
        => new(
            Enabled: authored.Enabled,
            Source: (int)authored.Source,
            CubemapRef: GetTexturePath(authored.ActiveCubemap),
            SolidColorRgb: ToArray(authored.SolidColorRgb),
            IlluminanceLux: authored.IlluminanceLux,
            Intensity: authored.Intensity,
            RotationRadians: authored.RotationRadians,
            TintRgb: ToArray(authored.TintRgb));

    // The fog cubemap and its distances stay at native defaults: the renderer
    // does not sample an inscattering cubemap.
    private static NativeFogEnvironment CreateFog(FogEnvironmentData authored)
        => new(
            Enabled: authored.Enabled,
            HeightFogEnabled: authored.HeightFogEnabled,
            Density: authored.Density,
            HeightFalloff: authored.HeightFalloff,
            HeightOffsetMeters: authored.HeightOffsetMeters,
            MaxOpacity: authored.MaxOpacity,
            InscatteringLuminanceRgb: ToArray(authored.InscatteringLuminanceRgb),
            SkyAmbientScaleRgb: ToArray(authored.SkyAmbientScaleRgb),
            SecondDensity: authored.SecondDensity,
            SecondHeightFalloff: authored.SecondHeightFalloff,
            SecondHeightOffsetMeters: authored.SecondHeightOffsetMeters,
            StartDistanceMeters: authored.StartDistanceMeters,
            EndDistanceMeters: authored.EndDistanceMeters,
            CutoffDistanceMeters: authored.CutoffDistanceMeters,
            DirectionalInscatteringLuminanceRgb: ToArray(authored.DirectionalInscatteringLuminanceRgb),
            DirectionalInscatteringExponent: authored.DirectionalInscatteringExponent,
            DirectionalInscatteringStartDistanceMeters: authored.DirectionalInscatteringStartDistanceMeters,
            VolumetricFogEnabled: authored.VolumetricFogEnabled,
            VolumetricScatteringDistribution: authored.VolumetricScatteringDistribution,
            VolumetricAlbedoRgb: ToArray(authored.VolumetricAlbedoRgb),
            VolumetricEmissiveRgb: ToArray(authored.VolumetricEmissiveRgb),
            VolumetricExtinctionScale: authored.VolumetricExtinctionScale,
            VolumetricDistanceMeters: authored.VolumetricDistanceMeters,
            VolumetricStartDistanceMeters: authored.VolumetricStartDistanceMeters,
            VolumetricNearFadeInDistanceMeters: authored.VolumetricNearFadeInDistanceMeters,
            VolumetricStaticLightingScatteringIntensity: authored.VolumetricStaticLightingScatteringIntensity,
            OverrideLightColorsWithFogInscattering: authored.OverrideLightColorsWithFogInscattering,
            RenderInMainPass: authored.RenderInMainPass,
            Holdout: authored.Holdout,
            VisibleInReflectionCaptures: authored.VisibleInReflectionCaptures,
            VisibleInRealTimeSkyCaptures: authored.VisibleInRealTimeSkyCaptures,
            Model: 0,
            SingleScatteringAlbedoRgb: [1.0f, 1.0f, 1.0f],
            AnisotropyG: 0.0f,
            InscatteringColorCubemapAngle: 0.0f,
            InscatteringTextureTint: [1.0f, 1.0f, 1.0f],
            FullyDirectionalInscatteringColorDistance: 0.0f,
            NonDirectionalInscatteringColorDistance: 0.0f);

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
            AutoExposureMeteringMask: GetTexturePath(authored.AutoExposureMeteringMask),
            AutoExposureCompensationCurve: [.. authored.AutoExposureCompensationCurve.Select(static key => new NativeExposureCompensationKey(key.MeteredEv, key.CompensationEv))],
            BloomIntensity: authored.BloomIntensity,
            BloomThreshold: authored.BloomThreshold,
            Saturation: authored.Saturation,
            Contrast: authored.Contrast,
            VignetteIntensity: authored.VignetteIntensity,
            DisplayGamma: authored.DisplayGamma);

    private static string? GetTexturePath(AssetPath? texture)
        => texture is null ? null : ContentPipelinePaths.ToNativeDescriptorPath(texture, AssetFileTypes.Texture);

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

    private static string ToDescriptor(VisibilitySource source) => source switch
    {
        VisibilitySource.Inherit => "inherit",
        VisibilitySource.Shown => "shown",
        VisibilitySource.Hidden => "hidden",
        _ => throw new InvalidOperationException($"Scene visibility source mode {(int)source} is not defined."),
    };

    private static string ToDescriptor(ShadowSource source) => source switch
    {
        ShadowSource.Inherit => "inherit",
        ShadowSource.On => "on",
        ShadowSource.Off => "off",
        _ => throw new InvalidOperationException($"Shadow source mode {(int)source} is not defined."),
    };

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
