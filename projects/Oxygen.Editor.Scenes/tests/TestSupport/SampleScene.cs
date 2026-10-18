// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// The scene most tests run on. Its ids are fixed, so data rows can name them and failures show stable values.
/// </summary>
/// <remarks>
/// <code>
/// nodes (scene graph)                          explorer layout
/// A        position (1,0,0)                    A
/// ├─ A1    90° about Y, scale 2                ├─ A1
/// │  └─ A11                                    │  └─ A11
/// └─ A2                                        └─ folder AF (expanded)
/// B                                                └─ A2
/// Cam      perspective, near 0.1, far 100      folder F (expanded)
/// Ortho    orthographic, size 10               ├─ B
/// Sun      directional, slot Primary           └─ folder G (collapsed, empty)
/// Moon     directional, slot None              Cam, Ortho, Sun, Moon, Lamp,
/// Lamp     point light, source radius 0.05     Spot, Fog, Mesh, Script
/// Spot     spot light, cones 0.4 / 0.6
/// Fog      local fog volume
/// Mesh     Cube.ogeo, one material slot override (Red.omat)
/// Script   one script slot (Move.oscript) whose "target" value is node B
/// </code>
/// Every id is fixed, so two builds are the same document. Ids end in readable tags: nodes are <c>...-0001-...</c>,
/// components <c>...-0002-...</c>, folders <c>...-0003-...</c>, slots <c>...-0004-...</c>; a node's transform is
/// <see cref="SceneBuilder.TransformId"/>.
/// </remarks>
public static class SampleScene
{
    public static Uri CubeGeometry { get; } = new("asset:/Content/Cube.ogeo");

    public static Uri SphereGeometry { get; } = new("asset:/Content/Sphere.ogeo");

    public static Uri RedMaterial { get; } = new("asset:/Content/Red.omat");

    public static Uri MoveScript { get; } = new("asset:/Content/Move.oscript");

    public static Uri PlayInput { get; } = new("asset:/Content/Play.oimap");

    /// <summary>Gets the layout revision of the cube's slot inventory: 64 hex digits, as a cooked inventory has.</summary>
    public static string CubeLayoutRevision { get; } = new('c', 64);

    public static SceneId Id { get; } = SceneId.FromValue(Tagged(0, "5CE"));

    public static NodeId A { get; } = NodeTag("A");

    public static NodeId A1 { get; } = NodeTag("A1");

    public static NodeId A11 { get; } = NodeTag("A11");

    public static NodeId A2 { get; } = NodeTag("A2");

    public static NodeId B { get; } = NodeTag("B");

    public static NodeId Cam { get; } = NodeTag("CA");

    public static NodeId Ortho { get; } = NodeTag("0C");

    public static NodeId Sun { get; } = NodeTag("50");

    public static NodeId Moon { get; } = NodeTag("60");

    public static NodeId Lamp { get; } = NodeTag("70");

    public static NodeId Spot { get; } = NodeTag("80");

    public static NodeId Fog { get; } = NodeTag("F0");

    public static NodeId Mesh { get; } = NodeTag("E0");

    public static NodeId Script { get; } = NodeTag("5C");

    public static FolderId AF { get; } = FolderId.FromValue(Tagged(3, "AF"));

    public static FolderId F { get; } = FolderId.FromValue(Tagged(3, "F"));

    public static FolderId G { get; } = FolderId.FromValue(Tagged(3, "F1"));

    public static ComponentId CamCamera { get; } = ComponentTag("CA");

    public static ComponentId OrthoCamera { get; } = ComponentTag("0C");

    public static ComponentId SunLight { get; } = ComponentTag("50");

    public static ComponentId MoonLight { get; } = ComponentTag("60");

    public static ComponentId LampLight { get; } = ComponentTag("70");

    public static ComponentId SpotLight { get; } = ComponentTag("80");

    public static ComponentId FogVolume { get; } = ComponentTag("F0");

    public static ComponentId MeshGeometry { get; } = ComponentTag("E0");

    public static ComponentId ScriptScripting { get; } = ComponentTag("5C");

    public static Guid CubeSlot { get; } = Tagged(4, "C1");

    public static Guid MoveSlot { get; } = Tagged(4, "51");

    /// <summary>Gets every node, in declaration order.</summary>
    public static IReadOnlyList<NodeId> AllNodes { get; } = [A, A1, A11, A2, B, Cam, Ortho, Sun, Moon, Lamp, Spot, Fog, Mesh, Script];

    /// <summary>
    /// Builds a new copy of the sample scene's data.
    /// </summary>
    /// <returns>The scene data.</returns>
    public static SceneFileData Build() => Builder().Build();

    /// <summary>
    /// Gets a builder that holds the sample scene, for a test that needs a variant of it.
    /// </summary>
    /// <returns>A new builder.</returns>
    public static SceneBuilder Builder()
        => new SceneBuilder()
            .WithId(Id)
            .Named("Sample")
            .Node(A, "A", transform: Transform(position: new Vector3(1, 0, 0)))
            .Node(A1, "A1", parent: A, transform: Transform(rotation: Quaternion.CreateFromAxisAngle(Vector3.UnitY, MathF.PI / 2), scale: new Vector3(2)))
            .Node(A11, "A11", parent: A1)
            .Folder(AF, "AF", underNode: A)
            .Node(A2, "A2", parent: A, folder: AF)
            .Folder(F, "F")
            .Node(B, "B", folder: F)
            .Folder(G, "G", inFolder: F, isExpanded: false)
            .Node(Cam, "Cam", components: [new PerspectiveCameraData { Id = CamCamera, Name = "Camera", NearPlane = 0.1f, FarPlane = 100f, AspectMode = CameraAspectMode.Auto }])
            .Node(Ortho, "Ortho", components: [new OrthographicCameraData { Id = OrthoCamera, Name = "Camera", NearPlane = 0.1f, FarPlane = 100f, OrthographicSize = 10f, AspectMode = CameraAspectMode.Auto }])
            .Node(Sun, "Sun", components: [new DirectionalLightData { Id = SunLight, Name = "Light", AtmosphereSlot = AtmosphereLightSlot.Primary }])
            .Node(Moon, "Moon", components: [new DirectionalLightData { Id = MoonLight, Name = "Light", AtmosphereSlot = AtmosphereLightSlot.None }])
            .Node(Lamp, "Lamp", components: [new PointLightData { Id = LampLight, Name = "Light", SourceRadius = 0.05f }])
            .Node(Spot, "Spot", components: [new SpotLightData { Id = SpotLight, Name = "Light", InnerConeAngleRadians = 0.4f, OuterConeAngleRadians = 0.6f }])
            .Node(Fog, "Fog", components: [new LocalFogVolumeData { Id = FogVolume, Name = "Fog" }])
            .Node(Mesh, "Mesh", components: [CubeWithRedSlot()])
            .Node(Script, "Script", components: [MoveTowardB()]);

    /// <summary>
    /// Makes a fixed id with a readable tag, such as <c>00000000-0000-0000-0001-0000000000a1</c>.
    /// </summary>
    /// <param name="kind">The kind tag in the fourth group: 1 node, 2 component, 3 folder, 4 slot.</param>
    /// <param name="tag">Up to twelve hex digits.</param>
    /// <returns>The id.</returns>
    public static Guid Tagged(int kind, string tag)
        => Guid.Parse($"00000000-0000-0000-{kind:X4}-{tag.PadLeft(12, '0')}", System.Globalization.CultureInfo.InvariantCulture);

    /// <summary>
    /// Makes a transform.
    /// </summary>
    /// <param name="position">The local position.</param>
    /// <param name="rotation">The local rotation.</param>
    /// <param name="scale">The local scale.</param>
    /// <returns>The transform data.</returns>
    public static TransformData Transform(Vector3? position = null, Quaternion? rotation = null, Vector3? scale = null)
        => new()
        {
            Name = "Transform",
            Position = position ?? Vector3.Zero,
            Rotation = rotation ?? Quaternion.Identity,
            Scale = scale ?? Vector3.One,
        };

    private static NodeId NodeTag(string tag) => NodeId.FromValue(Tagged(1, tag));

    private static ComponentId ComponentTag(string tag) => ComponentId.FromValue(Tagged(2, tag));

    private static JsonElement NodeValue(NodeId node)
    {
        using var document = JsonDocument.Parse($"\"{node.Value}\"");
        return document.RootElement.Clone();
    }

    private static GeometryComponentData CubeWithRedSlot()
        => new()
        {
            Id = MeshGeometry,
            Name = "Geometry",
            GeometryUri = CubeGeometry,
            OverrideSlots =
            [
                new MaterialsSlotData
                {
                    GeometryUri = CubeGeometry,
                    SlotId = CubeSlot,
                    LayoutRevision = CubeLayoutRevision,
                    MaterialUri = RedMaterial,
                },
            ],
        };

    private static ScriptingData MoveTowardB()
        => new()
        {
            Id = ScriptScripting,
            Name = "Scripting",
            Slots =
            [
                new ScriptSlotData
                {
                    Id = MoveSlot,
                    Script = MoveScript,
                    Overrides = new Dictionary<string, ScriptValueData>(StringComparer.Ordinal)
                    {
                        ["target"] = new() { Type = "node", Value = NodeValue(B) },
                    },
                },
            ],
        };
}
