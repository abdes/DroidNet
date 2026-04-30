// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Oxygen.Managed.Assets.Import.Scenes;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Cook;

internal static class CookedSceneWriter
{
    private const byte AssetTypeScene = 3;
    private const byte SceneAssetVersion = 3;
    private const int AssetHeaderSize = 103;
    private const int SceneAssetDescSize = 139;
    private const int NodeRecordSize = 68;
    private const int ComponentTableDescSize = 20;
    private const int RenderableRecordSize = 40;
    private const int PerspectiveCameraRecordSize = 20;
    private const int DirectionalLightRecordSize = 92;
    private const int PointLightRecordSize = 56;
    private const int SpotLightRecordSize = 64;
    private const uint ComponentTypeRenderable = 0x4853454D; // 'MESH'
    private const uint ComponentTypePerspectiveCamera = 0x4D414350; // 'PCAM'
    private const uint ComponentTypeDirectionalLight = 0x54494C44; // 'DLIT'
    private const uint ComponentTypePointLight = 0x54494C50; // 'PLIT'
    private const uint ComponentTypeSpotLight = 0x54494C53; // 'SLIT'
    private const uint EnvironmentTypeSkyAtmosphere = 0;
    private const uint EnvironmentTypePostProcessVolume = 5;
    private const uint ToneMapperAcesFitted = 1;
    private const uint ExposureModeAuto = 2;
    private const int SceneEnvironmentBlockHeaderSize = 8;
    private const int SkyAtmosphereEnvironmentRecordSize = 168;
    private const int PostProcessVolumeEnvironmentRecordSize = 60;
    private const float DefaultFieldOfViewDegrees = 60.0f;

    private interface IWritableRecord
    {
        public void Write(BinaryWriter writer);
    }

    public static void Write(Stream output, SceneSource source, Func<string, AssetKey?> resolveGeometryKey)
    {
        ArgumentNullException.ThrowIfNull(output);
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(resolveGeometryKey);

        using var state = new SceneCookState();

        foreach (var root in source.Nodes)
        {
            AddNode(state, root, parentIndex: null, resolveGeometryKey);
        }

        var componentTables = BuildComponentTables(state);
        WriteScenePayload(output, source.Name, state, componentTables);
    }

    private static void AddNode(
        SceneCookState state,
        SceneNodeSource sourceNode,
        int? parentIndex,
        Func<string, AssetKey?> resolveGeometryKey)
    {
        var nodeIndex = state.Nodes.Count;
        var nameOffset = state.Strings.Add(sourceNode.Name);
        var record = new NodeRecord(
            NodeId: BuildNodeKey(sourceNode, nodeIndex),
            NameOffset: nameOffset,
            ParentIndex: parentIndex ?? nodeIndex,
            Flags: ToNodeFlags(sourceNode.Flags),
            Translation: sourceNode.Translation ?? Vector3.Zero,
            Rotation: sourceNode.Rotation ?? Quaternion.Identity,
            Scale: sourceNode.Scale ?? Vector3.One);
        state.Nodes.Add(record);

        if (!string.IsNullOrWhiteSpace(sourceNode.Mesh)
            && TryNormalizeAssetUri(sourceNode.Mesh, out var meshVirtualPath)
            && resolveGeometryKey(meshVirtualPath) is { } geometryKey)
        {
            state.Renderables.Add(new RenderableRecord(nodeIndex, geometryKey, default, Visible: 1));
        }

        if (sourceNode.PerspectiveCamera is { } camera)
        {
            state.PerspectiveCameras.Add(new PerspectiveCameraRecord(
                nodeIndex,
                ToEngineFieldOfViewRadians(camera.FieldOfView),
                camera.AspectRatio,
                camera.NearPlane,
                camera.FarPlane));
        }

        if (sourceNode.DirectionalLight is { } directionalLight)
        {
            state.DirectionalLights.Add(new DirectionalLightRecord(nodeIndex, directionalLight));
        }

        if (sourceNode.PointLight is { } pointLight)
        {
            state.PointLights.Add(new PointLightRecord(nodeIndex, pointLight));
        }

        if (sourceNode.SpotLight is { } spotLight)
        {
            state.SpotLights.Add(new SpotLightRecord(nodeIndex, spotLight));
        }

        if (sourceNode.Children is null)
        {
            return;
        }

        foreach (var child in sourceNode.Children)
        {
            AddNode(state, child, nodeIndex, resolveGeometryKey);
        }
    }

    private static List<ComponentTable> BuildComponentTables(SceneCookState state)
    {
        var componentTables = new List<ComponentTable>();
        AddComponentTable(componentTables, ComponentTypeRenderable, RenderableRecordSize, state.Renderables);
        AddComponentTable(componentTables, ComponentTypePerspectiveCamera, PerspectiveCameraRecordSize, state.PerspectiveCameras);
        AddComponentTable(componentTables, ComponentTypeDirectionalLight, DirectionalLightRecordSize, state.DirectionalLights);
        AddComponentTable(componentTables, ComponentTypePointLight, PointLightRecordSize, state.PointLights);
        AddComponentTable(componentTables, ComponentTypeSpotLight, SpotLightRecordSize, state.SpotLights);
        return componentTables;
    }

    private static void AddComponentTable<T>(
        List<ComponentTable> componentTables,
        uint componentType,
        int recordSize,
        IReadOnlyList<T> records)
        where T : IWritableRecord
    {
        if (records.Count == 0)
        {
            return;
        }

        componentTables.Add(new ComponentTable(
            componentType,
            recordSize,
            WriteRecords(records, static (writer, record) => record.Write(writer))));
    }

    private static void WriteScenePayload(
        Stream output,
        string sceneName,
        SceneCookState state,
        IReadOnlyList<ComponentTable> componentTables)
    {
        var nodesOffset = SceneAssetDescSize;
        var nodesBytes = WriteRecords(state.Nodes, static (writer, record) => record.Write(writer));
        var stringsOffset = nodesOffset + nodesBytes.Length;
        var stringsBytes = state.Strings.ToArray();
        var directoryOffset = componentTables.Count == 0 ? 0 : stringsOffset + stringsBytes.Length;

        var payloadOffset = directoryOffset + (componentTables.Count * ComponentTableDescSize);
        var directory = new List<ComponentTableDescriptor>(componentTables.Count);
        foreach (var table in componentTables)
        {
            directory.Add(new ComponentTableDescriptor(
                table.ComponentType,
                Offset: payloadOffset,
                Count: table.Bytes.Length / table.EntrySize,
                EntrySize: table.EntrySize));
            payloadOffset += table.Bytes.Length;
        }

        using var writer = new BinaryWriter(output, Encoding.UTF8, leaveOpen: true);
        WriteSceneAssetDesc(
            writer,
            sceneName,
            nodesOffset,
            state.Nodes.Count,
            stringsOffset,
            stringsBytes.Length,
            directoryOffset,
            componentTables.Count);
        writer.Write(nodesBytes);
        writer.Write(stringsBytes);
        foreach (var desc in directory)
        {
            desc.Write(writer);
        }

        foreach (var table in componentTables)
        {
            writer.Write(table.Bytes);
        }

        WriteDefaultSceneEnvironment(writer);
    }

    private static void WriteSceneAssetDesc(
        BinaryWriter writer,
        string sceneName,
        int nodesOffset,
        int nodeCount,
        int stringsOffset,
        int stringsLength,
        int directoryOffset,
        int directoryCount)
    {
        writer.Write(AssetTypeScene);
        WriteFixedUtf8(writer, sceneName, 64);
        writer.Write(SceneAssetVersion);
        writer.Write((byte)0); // streaming_priority
        writer.Write(new byte[32]); // content_hash
        writer.Write(0U); // variant_flags

        writer.Write((ulong)nodesOffset);
        writer.Write((uint)nodeCount);
        writer.Write((uint)NodeRecordSize);

        writer.Write((uint)stringsOffset);
        writer.Write((uint)stringsLength);

        writer.Write((ulong)directoryOffset);
        writer.Write((uint)directoryCount);
    }

    private static void WriteDefaultSceneEnvironment(BinaryWriter writer)
    {
        writer.Write((uint)(SceneEnvironmentBlockHeaderSize + SkyAtmosphereEnvironmentRecordSize + PostProcessVolumeEnvironmentRecordSize));
        writer.Write(2U);

        WriteDefaultSkyAtmosphere(writer);
        WriteDefaultPostProcessVolume(writer);
    }

    private static void WriteDefaultSkyAtmosphere(BinaryWriter writer)
    {
        writer.Write(EnvironmentTypeSkyAtmosphere);
        writer.Write((uint)SkyAtmosphereEnvironmentRecordSize);
        writer.Write(1U); // enabled

        ReadOnlySpan<float> values =
        [
            6_360_000.0f,
            80_000.0f,
            0.4f,
            0.4f,
            0.4f,
            5.8e-6f,
            13.5e-6f,
            33.1e-6f,
            8_000.0f,
            21.0e-6f,
            21.0e-6f,
            21.0e-6f,
            0.0f,
            0.0f,
            0.0f,
            1_200.0f,
            0.8f,
            0.0f,
            0.0f,
            0.0f,
            25_000.0f,
            0.0f,
            0.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            0.0f,
            1.0f,
            1.0f,
            -90.0f,
        ];

        foreach (var value in values)
        {
            writer.Write(value);
        }

        writer.Write(1U); // sun_disk_enabled
        writer.Write(0U); // holdout
        writer.Write(1U); // render_in_main_pass
    }

    private static void WriteDefaultPostProcessVolume(BinaryWriter writer)
    {
        writer.Write(EnvironmentTypePostProcessVolume);
        writer.Write((uint)PostProcessVolumeEnvironmentRecordSize);
        writer.Write(1U); // enabled
        writer.Write(ToneMapperAcesFitted);
        writer.Write(ExposureModeAuto);
        writer.Write(0.0f); // exposure_compensation_ev
        writer.Write(-6.0f); // auto_exposure_min_ev
        writer.Write(16.0f); // auto_exposure_max_ev
        writer.Write(3.0f); // auto_exposure_speed_up
        writer.Write(1.0f); // auto_exposure_speed_down
        writer.Write(0.0f); // bloom_intensity
        writer.Write(1.0f); // bloom_threshold
        writer.Write(1.0f); // saturation
        writer.Write(1.0f); // contrast
        writer.Write(0.0f); // vignette_intensity
    }

    private static byte[] WriteRecords<T>(IReadOnlyList<T> records, Action<BinaryWriter, T> write)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        foreach (var record in records)
        {
            write(writer, record);
        }

        return stream.ToArray();
    }

    private static void WriteFixedUtf8(BinaryWriter writer, string? value, int size)
    {
        Span<byte> buffer = stackalloc byte[size];
        if (!string.IsNullOrEmpty(value))
        {
            var bytes = Encoding.UTF8.GetBytes(value);
            bytes.AsSpan(0, Math.Min(bytes.Length, size - 1)).CopyTo(buffer);
        }

        writer.Write(buffer);
    }

    private static AssetKey BuildNodeKey(SceneNodeSource node, int fallbackIndex)
    {
        var source = node.Id?.ToString("D") ?? string.Create(System.Globalization.CultureInfo.InvariantCulture, $"{node.Name}:{fallbackIndex}");
        var bytes = Encoding.UTF8.GetBytes(source);
        Span<byte> hash = stackalloc byte[32];
        _ = SHA256.HashData(bytes, hash);
        return new AssetKey(
            BinaryPrimitives.ReadUInt64LittleEndian(hash[..8]),
            BinaryPrimitives.ReadUInt64LittleEndian(hash.Slice(8, 8)));
    }

    private static uint ToNodeFlags(SceneNodeFlagsSource? flags)
    {
        flags ??= new SceneNodeFlagsSource();
        uint value = 0;
        if (flags.Visible)
        {
            value |= 1U << 0;
        }

        if (flags.Static)
        {
            value |= 1U << 1;
        }

        if (flags.CastsShadows)
        {
            value |= 1U << 2;
        }

        if (flags.ReceivesShadows)
        {
            value |= 1U << 3;
        }

        if (flags.RayCastingSelectable)
        {
            value |= 1U << 4;
        }

        if (flags.IgnoreParentTransform)
        {
            value |= 1U << 5;
        }

        return value;
    }

    private static bool TryNormalizeAssetUri(string assetUri, out string virtualPath)
    {
        virtualPath = string.Empty;
        if (!Uri.TryCreate(assetUri, UriKind.Absolute, out var uri)
            || !string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        virtualPath = Uri.UnescapeDataString(uri.AbsolutePath);
        return virtualPath.StartsWith('/') && virtualPath.Length > 1;
    }

    private static float ToEngineFieldOfViewRadians(float fieldOfViewDegrees)
    {
        var degrees = float.IsFinite(fieldOfViewDegrees) && fieldOfViewDegrees > 0.0f
            ? fieldOfViewDegrees
            : DefaultFieldOfViewDegrees;

        return degrees * (MathF.PI / 180.0f);
    }

    private static void WriteLightCommon(BinaryWriter writer, LightCommonSource common)
    {
        writer.Write(common.AffectsWorld ? 1U : 0U);
        writer.Write(common.Red);
        writer.Write(common.Green);
        writer.Write(common.Blue);
        writer.Write((byte)0); // LightMobility::kRealtime
        writer.Write((byte)(common.CastsShadows ? 1 : 0));
        writer.Write(0.0006f); // shadow.bias
        writer.Write(0.02f); // shadow.normal_bias
        writer.Write(0U); // shadow.contact_shadows
        writer.Write((byte)1); // ShadowResolutionHint::kMedium
        writer.Write(common.ExposureCompensation);
    }

    [StructLayout(LayoutKind.Auto)]
    private readonly record struct ComponentTable(uint ComponentType, int EntrySize, byte[] Bytes);

    [StructLayout(LayoutKind.Auto)]
    private readonly record struct ComponentTableDescriptor(uint ComponentType, int Offset, int Count, int EntrySize) : IWritableRecord
    {
        public void Write(BinaryWriter writer)
        {
            writer.Write(this.ComponentType);
            writer.Write((ulong)this.Offset);
            writer.Write((uint)this.Count);
            writer.Write((uint)this.EntrySize);
        }
    }

    [StructLayout(LayoutKind.Auto)]
    private readonly record struct NodeRecord(
        AssetKey NodeId,
        uint NameOffset,
        int ParentIndex,
        uint Flags,
        Vector3 Translation,
        Quaternion Rotation,
        Vector3 Scale) : IWritableRecord
    {
        public void Write(BinaryWriter writer)
        {
            Span<byte> key = stackalloc byte[16];
            this.NodeId.WriteBytes(key);
            writer.Write(key);
            writer.Write(this.NameOffset);
            writer.Write((uint)this.ParentIndex);
            writer.Write(this.Flags);
            writer.Write(this.Translation.X);
            writer.Write(this.Translation.Y);
            writer.Write(this.Translation.Z);
            writer.Write(this.Rotation.X);
            writer.Write(this.Rotation.Y);
            writer.Write(this.Rotation.Z);
            writer.Write(this.Rotation.W);
            writer.Write(this.Scale.X);
            writer.Write(this.Scale.Y);
            writer.Write(this.Scale.Z);
        }
    }

    [StructLayout(LayoutKind.Auto)]
    private readonly record struct RenderableRecord(int NodeIndex, AssetKey GeometryKey, AssetKey MaterialKey, uint Visible) : IWritableRecord
    {
        public void Write(BinaryWriter writer)
        {
            writer.Write((uint)this.NodeIndex);
            Span<byte> key = stackalloc byte[16];
            this.GeometryKey.WriteBytes(key);
            writer.Write(key);
            this.MaterialKey.WriteBytes(key);
            writer.Write(key);
            writer.Write(this.Visible);
        }
    }

    [StructLayout(LayoutKind.Auto)]
    private readonly record struct PerspectiveCameraRecord(
        int NodeIndex,
        float FieldOfView,
        float AspectRatio,
        float NearPlane,
        float FarPlane) : IWritableRecord
    {
        public void Write(BinaryWriter writer)
        {
            writer.Write((uint)this.NodeIndex);
            writer.Write(this.FieldOfView);
            writer.Write(this.AspectRatio);
            writer.Write(this.NearPlane);
            writer.Write(this.FarPlane);
        }
    }

    [StructLayout(LayoutKind.Auto)]
    private readonly record struct DirectionalLightRecord(int NodeIndex, DirectionalLightSource Source) : IWritableRecord
    {
        public void Write(BinaryWriter writer)
        {
            writer.Write((uint)this.NodeIndex);
            WriteLightCommon(writer, this.Source.Common);
            writer.Write(this.Source.AngularSizeRadians);
            writer.Write(this.Source.EnvironmentContribution ? 1U : 0U);
            writer.Write(this.Source.IsSunLight ? 1U : 0U);
            writer.Write(4U); // cascade_count
            writer.Write(8.0f);
            writer.Write(24.0f);
            writer.Write(64.0f);
            writer.Write(160.0f);
            writer.Write(3.0f); // distribution_exponent
            writer.Write((byte)0); // DirectionalCsmSplitMode::kGenerated
            writer.Write(160.0f); // max_shadow_distance
            writer.Write(0.1f); // transition_fraction
            writer.Write(0.1f); // distance_fadeout_fraction
            writer.Write(this.Source.IntensityLux);
        }
    }

    [StructLayout(LayoutKind.Auto)]
    private readonly record struct PointLightRecord(int NodeIndex, PointLightSource Source) : IWritableRecord
    {
        public void Write(BinaryWriter writer)
        {
            writer.Write((uint)this.NodeIndex);
            WriteLightCommon(writer, this.Source.Common);
            writer.Write(this.Source.Range);
            writer.Write(this.Source.DecayExponent);
            writer.Write(this.Source.SourceRadius);
            writer.Write(this.Source.LuminousFluxLumens);
            writer.Write((byte)0); // AttenuationModel::kInverseSquare
        }
    }

    [StructLayout(LayoutKind.Auto)]
    private readonly record struct SpotLightRecord(int NodeIndex, SpotLightSource Source) : IWritableRecord
    {
        public void Write(BinaryWriter writer)
        {
            writer.Write((uint)this.NodeIndex);
            WriteLightCommon(writer, this.Source.Common);
            writer.Write(this.Source.Range);
            writer.Write(this.Source.DecayExponent);
            writer.Write(this.Source.InnerConeAngleRadians);
            writer.Write(this.Source.OuterConeAngleRadians);
            writer.Write(this.Source.SourceRadius);
            writer.Write(this.Source.LuminousFluxLumens);
            writer.Write((byte)0); // AttenuationModel::kInverseSquare
        }
    }

    private sealed class StringTableBuilder : IDisposable
    {
        private readonly MemoryStream bytes = new();

        public StringTableBuilder()
        {
            this.bytes.WriteByte(0);
        }

        public uint Add(string? value)
        {
            if (string.IsNullOrEmpty(value))
            {
                return 0;
            }

            var offset = checked((uint)this.bytes.Length);
            var encoded = Encoding.UTF8.GetBytes(value);
            this.bytes.Write(encoded);
            this.bytes.WriteByte(0);
            return offset;
        }

        public byte[] ToArray() => this.bytes.ToArray();

        public void Dispose() => this.bytes.Dispose();
    }

    private sealed class SceneCookState : IDisposable
    {
        public StringTableBuilder Strings { get; } = new();

        public List<NodeRecord> Nodes { get; } = [];

        public List<RenderableRecord> Renderables { get; } = [];

        public List<PerspectiveCameraRecord> PerspectiveCameras { get; } = [];

        public List<DirectionalLightRecord> DirectionalLights { get; } = [];

        public List<PointLightRecord> PointLights { get; } = [];

        public List<SpotLightRecord> SpotLights { get; } = [];

        public void Dispose() => this.Strings.Dispose();
    }
}
