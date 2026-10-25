// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.TestSupport;

/// <summary>
/// Provides shared test fixtures, builders, and stream helpers for World unit tests.
/// </summary>
[ExcludeFromCodeCoverage]
internal static class SceneTestSupport
{
    /// <summary>
    /// Creates the four required component DTOs of a node, in canonical order with the hierarchy last.
    /// </summary>
    /// <param name="name">The node name.</param>
    /// <param name="children">The hierarchy's children, or <see langword="null"/> for a leaf.</param>
    /// <returns>A mutable list, so a test can drop or repeat an entry.</returns>
    public static List<ComponentData> RequiredComponents(string name, IList<SceneNodeData>? children = null)
        =>
        [
            new MetadataData { Name = name },
            new TransformData { Name = "Transform" },
            new NodeFlagsData { Name = NodeFlagsComponent.DefaultName },
            new HierarchyData { Name = HierarchyComponent.DefaultName, Children = children },
        ];

    /// <summary>
    /// Creates a node DTO holding the required components and the given optional ones.
    /// </summary>
    /// <param name="name">The node name.</param>
    /// <param name="optional">Optional component DTOs.</param>
    /// <returns>The node DTO.</returns>
    public static SceneNodeData NodeData(string name, params ComponentData[] optional)
    {
        var components = RequiredComponents(name);
        components.InsertRange(3, optional);
        return new SceneNodeData { Id = NodeId.New(), Components = components };
    }

    public static async Task<MemoryStream> SerializeToStreamAsync(Scene scene)
    {
        var stream = new MemoryStream();
        await SceneSerializer.SerializeAsync(stream, scene).ConfigureAwait(false);
        stream.Position = 0;
        return stream;
    }

    public static async Task<Scene> DeserializeFromStreamAsync(Stream stream)
    {
        stream.Position = 0;
        return await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);
    }

    public static async Task<Scene> RoundTripAsync(Scene scene)
    {
        var stream = await SerializeToStreamAsync(scene).ConfigureAwait(false);
        await using var lifetime = stream.ConfigureAwait(false);
        return await DeserializeFromStreamAsync(stream).ConfigureAwait(false);
    }

    public static Scene CreateCompleteScene()
    {
        var scene = new Scene { Name = "Complete Authored Scene" };
        scene.SetEnvironment(CreateCompleteEnvironment());
        scene.SetReferences(new SceneReferencesData
        {
            Scripts = [AssetPath.Parse("/Content/Scripts/Controller.oscript")],
            InputActions = [AssetPath.Parse("/Content/Input/Move.oiact")],
            PhysicsSidecars = [AssetPath.Parse("/Content/Physics/Colliders.opscene")],
            ExtraAssets = [AssetPath.Parse("/Content/Textures/Noise.otex")],
        });
        scene.SetStartingInputContexts([
            new StartingInputContextData(AssetPath.Parse("/Content/Input/Gameplay.oimap"), Priority: 1),
        ]);

        var cameraNode = CreateCameraNode(scene);
        scene.RootNodes.Add(cameraNode);
        scene.SetStartingCamera(NodeReference.To(cameraNode.Id));

        var heroNode = CreateHeroNode(scene, cameraNode.Id);
        heroNode.Hierarchy.AddChild(CreateChildNode(scene));
        scene.RootNodes.Add(heroNode);

        return scene;
    }

    private static SceneEnvironmentData CreateCompleteEnvironment() => new()
    {
        AtmosphereEnabled = true,
        Background = new BackgroundEnvironmentData { Enabled = true, ColorRgb = new Vector3(0.1f, 0.2f, 0.3f) },
        SkySphere = new SkySphereEnvironmentData
        {
            Enabled = true,
            Source = SkySphereSource.SolidColor,
            SolidColorRgb = new Vector3(0.5f, 0.6f, 0.7f),
            Intensity = 120f,
            RotationRadians = 0.5f,
            TintRgb = new Vector3(1f, 1f, 1f),
        },
        SkyLight = new SkyLightEnvironmentData
        {
            Enabled = true,
            Source = SkyLightSource.SpecifiedCubemap,
            Cubemap = AssetPath.Parse("/Content/Light.otex.json"),
            Intensity = 1.5f,
            TintRgb = new Vector3(0.9f, 0.9f, 1f),
            DiffuseIntensity = 0.8f,
            SpecularIntensity = 1.0f,
        },
        SkyAtmosphere = new SkyAtmosphereEnvironmentData
        {
            PlanetRadiusMeters = 6_371_000f,
            AtmosphereHeightMeters = 100_000f,
            GroundAlbedoRgb = new Vector3(0.2f, 0.2f, 0.2f),
        },
        Fog = new FogEnvironmentData
        {
            Enabled = true,
            Density = 0.02f,
            InscatteringLuminanceRgb = new Vector3(0.3f, 0.4f, 0.5f),
        },
        PostProcess = new PostProcessEnvironmentData
        {
            ExposureMode = ExposureMode.ManualCamera,
            ManualExposureEv = 10f,
            BloomIntensity = 0.5f,
        },
    };

    private static SceneNode CreateCameraNode(Scene scene)
    {
        var cameraNode = new SceneNode(scene, "Starting Camera Node");
        var perspectiveCamera = new PerspectiveCamera
        {
            Name = "Main Camera",
            FieldOfView = 75f,
            AspectRatio = 16f / 9f,
            AspectMode = CameraAspectMode.Fixed,
            NearPlane = 0.1f,
            FarPlane = 1000f,
            ApertureF = 4f,
            ShutterRate = 200f,
            Iso = 200f,
        };
        _ = cameraNode.AddComponent(perspectiveCamera);
        return cameraNode;
    }

    private static SceneNode CreateHeroNode(Scene scene, NodeId cameraNodeId)
    {
        var heroNode = new SceneNode(scene, "Hero Node");
        heroNode.Flags.Visibility = VisibilitySource.Shown;
        heroNode.Flags.CastsShadows = ShadowSource.On;
        heroNode.Flags.ReceivesShadows = ShadowSource.On;

        _ = heroNode.AddComponent(CreateHeroGeometry());
        _ = heroNode.AddComponent(new DirectionalLightComponent
        {
            Name = "Sun",
            AtmosphereSlot = AtmosphereLightSlot.Primary,
            IntensityLux = 110_000f,
            Color = new Vector3(1f, 0.95f, 0.9f),
            CastsShadows = true,
        });

        _ = heroNode.AddComponent(new LocalFogVolumeComponent
        {
            Name = "Ground Fog",
            RadialFogExtinction = 1.5f,
            HeightFogExtinction = 0.8f,
            FogAlbedo = new Vector3(0.8f, 0.8f, 0.85f),
        });

        _ = heroNode.AddComponent(CreateHeroScripting(cameraNodeId));
        return heroNode;
    }

    private static GeometryComponent CreateHeroGeometry()
    {
        var geometry = new GeometryComponent
        {
            Name = "Hero Mesh",
            Geometry = AssetPath.Parse("/Content/Meshes/Hero.geometry"),
        };
        var revision = new string('a', 64);
        geometry.Overrides = geometry.Overrides
            .With(SceneSchema.Geometry.Surface.Material, Surface("20000000-0000-0000-0000-000000000001"), revision, AssetPath.Parse("/Content/Materials/HeroSkin.omat.json"))
            .With(SceneSchema.Geometry.Surface.Material, Surface("20000000-0000-0000-0000-000000000002"), revision, AssetPath.Parse("/Content/Materials/HeroArmor.omat.json"));
        return geometry;

        static PartId Surface(string id) => new(SceneSchema.Geometry.Surface.Kind, Guid.Parse(id));
    }

    private static ScriptingComponent CreateHeroScripting(NodeId cameraNodeId)
        => new()
        {
            Name = ScriptingComponent.DefaultName,
            Slots =
            [
                new ScriptSlot
                {
                    Script = AssetPath.Parse("/Content/Scripts/Controller.oscript"),
                    ExecutionOrder = 10,
                }
                .WithOverride("speed", ScriptValue.FromFloat(5.5f))
                .WithOverride("target", ScriptValue.FromNode(cameraNodeId)),
            ],
        };

    private static SceneNode CreateChildNode(Scene scene)
    {
        var childNode = new SceneNode(scene, "Child Node");
        _ = childNode.AddComponent(new OrthographicCamera
        {
            Name = "Minimap Camera",
            OrthographicSize = 50f,
            AspectRatio = 1f,
            AspectMode = CameraAspectMode.Fixed,
        });

        _ = childNode.AddComponent(new PointLightComponent
        {
            Name = "Torch",
            LuminousFluxLumens = 1200f,
            Range = 15f,
            SourceRadius = 0.2f,
            Color = new Vector3(1f, 0.6f, 0.2f),
        });

        var spotLightChild = new SceneNode(scene, "Spotlight Node");
        _ = spotLightChild.AddComponent(new SpotLightComponent
        {
            Name = "Flashlight",
            LuminousFluxLumens = 1500f,
            Range = 25f,
            InnerConeAngleRadians = 0.3f,
            OuterConeAngleRadians = 0.5f,
        });
        childNode.Hierarchy.AddChild(spotLightChild);
        return childNode;
    }
}
