// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using Oxygen.Assets.Import.Scenes;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Core;

namespace Oxygen.Assets.Cook;

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

    public static void Write(Stream output, SceneSource source, Func<string, AssetKey?> resolveGeometryKey)
    {
        ArgumentNullException.ThrowIfNull(output);
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(resolveGeometryKey);

        var strings = new StringTableBuilder();
        var nodes = new List<NodeRecord>();
        var renderables = new List<RenderableRecord>();
        var perspectiveCameras = new List<PerspectiveCameraRecord>();
        var directionalLights = new List<DirectionalLightRecord>();
        var pointLights = new List<PointLightRecord>();
        var spotLights = new List<SpotLightRecord>();

        foreach (var root in source.Nodes)
        {
            AddNode(root, parentIndex: null);
        }

        var componentTables = new List<ComponentTable>();
        if (renderables.Count > 0)
        {
            componentTables.Add(new ComponentTable(
                ComponentTypeRenderable,
                RenderableRecordSize,
                WriteRecords(renderables, static (writer, record) => record.Write(writer))));
        }

        if (perspectiveCameras.Count > 0)
        {
            componentTables.Add(new ComponentTable(
                ComponentTypePerspectiveCamera,
                PerspectiveCameraRecordSize,
                WriteRecords(perspectiveCameras, static (writer, record) => record.Write(writer))));
        }

        if (directionalLights.Count > 0)
        {
            componentTables.Add(new ComponentTable(
                ComponentTypeDirectionalLight,
                DirectionalLightRecordSize,
                WriteRecords(directionalLights, static (writer, record) => record.Write(writer))));
        }

        if (pointLights.Count > 0)
        {
            componentTables.Add(new ComponentTable(
                ComponentTypePointLight,
                PointLightRecordSize,
                WriteRecords(pointLights, static (writer, record) => record.Write(writer))));
        }

        if (spotLights.Count > 0)
        {
            componentTables.Add(new ComponentTable(
                ComponentTypeSpotLight,
                SpotLightRecordSize,
                WriteRecords(spotLights, static (writer, record) => record.Write(writer))));
        }

        var nodesOffset = SceneAssetDescSize;
        var nodesBytes = WriteRecords(nodes, static (writer, record) => record.Write(writer));
        var stringsOffset = nodesOffset + nodesBytes.Length;
        var stringsBytes = strings.ToArray();
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
            source.Name,
            nodesOffset,
            nodes.Count,
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

        void AddNode(SceneNodeSource sourceNode, int? parentIndex)
        {
            var nodeIndex = nodes.Count;
            var nameOffset = strings.Add(sourceNode.Name);
            var record = new NodeRecord(
                NodeId: BuildNodeKey(sourceNode, nodeIndex),
                NameOffset: nameOffset,
                ParentIndex: parentIndex ?? nodeIndex,
                Flags: ToNodeFlags(sourceNode.Flags),
                Translation: sourceNode.Translation ?? Vector3.Zero,
                Rotation: sourceNode.Rotation ?? Quaternion.Identity,
                Scale: sourceNode.Scale ?? Vector3.One);
            nodes.Add(record);

            if (!string.IsNullOrWhiteSpace(sourceNode.Mesh)
                && TryNormalizeAssetUri(sourceNode.Mesh, out var meshVirtualPath)
                && resolveGeometryKey(meshVirtualPath) is { } geometryKey)
            {
                renderables.Add(new RenderableRecord(nodeIndex, geometryKey, default, Visible: 1));
            }

            if (sourceNode.PerspectiveCamera is { } camera)
            {
                perspectiveCameras.Add(new PerspectiveCameraRecord(
                    nodeIndex,
                    ToEngineFieldOfViewRadians(camera.FieldOfView),
                    camera.AspectRatio,
                    camera.NearPlane,
                    camera.FarPlane));
            }

            if (sourceNode.DirectionalLight is { } directionalLight)
            {
                directionalLights.Add(new DirectionalLightRecord(nodeIndex, directionalLight));
            }

            if (sourceNode.PointLight is { } pointLight)
            {
                pointLights.Add(new PointLightRecord(nodeIndex, pointLight));
            }

            if (sourceNode.SpotLight is { } spotLight)
            {
                spotLights.Add(new SpotLightRecord(nodeIndex, spotLight));
            }

            if (sourceNode.Children is null)
            {
                return;
            }

            foreach (var child in sourceNode.Children)
            {
                AddNode(child, nodeIndex);
            }
        }
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
        writer.Write((uint)0); // variant_flags

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

        writer.Write(6_360_000.0f); // planet_radius_m
        writer.Write(80_000.0f); // atmosphere_height_m
        writer.Write(0.4f);
        writer.Write(0.4f);
        writer.Write(0.4f);
        writer.Write(5.8e-6f);
        writer.Write(13.5e-6f);
        writer.Write(33.1e-6f);
        writer.Write(8_000.0f);
        writer.Write(21.0e-6f);
        writer.Write(21.0e-6f);
        writer.Write(21.0e-6f);
        writer.Write(0.0f);
        writer.Write(0.0f);
        writer.Write(0.0f);
        writer.Write(1_200.0f);
        writer.Write(0.8f);
        writer.Write(0.0f);
        writer.Write(0.0f);
        writer.Write(0.0f);
        writer.Write(25_000.0f);
        writer.Write(0.0f);
        writer.Write(0.0f);
        writer.Write(1.0f);
        writer.Write(1.0f);
        writer.Write(1.0f);
        writer.Write(1.0f);
        writer.Write(1.0f);
        writer.Write(1.0f);
        writer.Write(1.0f);
        writer.Write(1.0f);
        writer.Write(1.0f);
        writer.Write(0.0f);
        writer.Write(1.0f);
        writer.Write(1.0f);
        writer.Write(-90.0f);
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
        return virtualPath.StartsWith("/", StringComparison.Ordinal) && virtualPath.Length > 1;
    }

    private static float ToEngineFieldOfViewRadians(float fieldOfViewDegrees)
    {
        var degrees = float.IsFinite(fieldOfViewDegrees) && fieldOfViewDegrees > 0.0f
            ? fieldOfViewDegrees
            : DefaultFieldOfViewDegrees;

        return degrees * (MathF.PI / 180.0f);
    }

    private sealed class StringTableBuilder
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
    }

    private readonly record struct ComponentTable(uint ComponentType, int EntrySize, byte[] Bytes);

    private readonly record struct ComponentTableDescriptor(uint ComponentType, int Offset, int Count, int EntrySize)
    {
        public void Write(BinaryWriter writer)
        {
            writer.Write(this.ComponentType);
            writer.Write((ulong)this.Offset);
            writer.Write((uint)this.Count);
            writer.Write((uint)this.EntrySize);
        }
    }

    private readonly record struct NodeRecord(
        AssetKey NodeId,
        uint NameOffset,
        int ParentIndex,
        uint Flags,
        Vector3 Translation,
        Quaternion Rotation,
        Vector3 Scale)
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

    private readonly record struct RenderableRecord(int NodeIndex, AssetKey GeometryKey, AssetKey MaterialKey, uint Visible)
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

    private readonly record struct PerspectiveCameraRecord(
        int NodeIndex,
        float FieldOfView,
        float AspectRatio,
        float NearPlane,
        float FarPlane)
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

    private readonly record struct DirectionalLightRecord(int NodeIndex, DirectionalLightSource Source)
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

    private readonly record struct PointLightRecord(int NodeIndex, PointLightSource Source)
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

    private readonly record struct SpotLightRecord(int NodeIndex, SpotLightSource Source)
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
}
