// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Assets.Import;
using Oxygen.Assets.Import.Gltf;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using SharpGLTF.Geometry;
using SharpGLTF.Geometry.VertexTypes;
using SharpGLTF.Materials;
using SharpGLTF.Scenes;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class GltfGeometryImporterTests
{
    [TestMethod]
    public async Task ImportAsync_ShouldWriteCookedOgeoAndBuffersAndIndex_ForGlb()
    {
        const string sourcePath = "Content/Geometry/Tri.glb";

        var files = new InMemoryImportFileAccess();
        files.AddBytes(sourcePath, CreateTriangleGlb());

        var registry = new ImporterRegistry();
        registry.Register(new GltfGeometryImporter());

        var import = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static () => new FixedIdentityPolicy(new AssetKey(11, 22)));

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: "Content")],
            Options: new ImportOptions(FailFast: true));

        var result = await import.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeTrue();
        _ = result.Imported.Should().ContainSingle();

        _ = files.TryGet(".cooked/Content/container.index.bin", out var indexBytes).Should().BeTrue();
        var doc = ReadIndex(indexBytes);

        AssertCookedOutputs(files, doc);
    }

    [TestMethod]
    public async Task ImportAsync_ShouldWriteCookedOgeoAndBuffersAndIndex_ForGltf()
    {
        const string sourcePath = "Content/Geometry/Tri.gltf";
        const string bufferPath = "Content/Geometry/buffer0.bin";

        var files = new InMemoryImportFileAccess();
        files.AddBytes(sourcePath, CreateTriangleGltfJson());
        files.AddBytes(bufferPath, CreateTriangleBin());

        var registry = new ImporterRegistry();
        registry.Register(new GltfGeometryImporter());

        var import = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static () => new FixedIdentityPolicy(new AssetKey(11, 22)));

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: "Content")],
            Options: new ImportOptions(FailFast: true));

        var result = await import.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeTrue();
        _ = result.Imported.Should().NotBeEmpty();

        _ = files.TryGet(".cooked/Content/container.index.bin", out var indexBytes).Should().BeTrue();
        var doc = ReadIndex(indexBytes);

        AssertCookedOutputs(files, doc);
    }

    private static Document ReadIndex(byte[] indexBytes)
    {
        using var ms = new MemoryStream(indexBytes);
        return LooseCookedIndex.Read(ms);
    }

    private static void AssertCookedOutputs(InMemoryImportFileAccess files, Document doc)
    {
        _ = doc.Assets.Should().ContainSingle();
        var entry = doc.Assets[0];

        _ = entry.VirtualPath.Should().Be("/Content/Geometry/Tri__mesh__0000.ogeo");
        _ = entry.AssetType.Should().Be(2);

        _ = files.TryGet(".cooked/Content/resources/buffers.table", out _).Should().BeTrue();
        _ = files.TryGet(".cooked/Content/resources/buffers.data", out _).Should().BeTrue();

        _ = files.TryGet(".cooked/Content/Geometry/Tri__mesh__0000.ogeo", out var ogeoBytes).Should().BeTrue();
        _ = entry.DescriptorSize.Should().Be((ulong)ogeoBytes.Length);
        _ = entry.DescriptorSize.Should().BeGreaterThan(256);
        _ = entry.DescriptorSha256.Span.ToArray().Should().Equal(LooseCookedIndex.ComputeSha256(ogeoBytes));
    }

    private static byte[] CreateTriangleGlb()
    {
        var material = new MaterialBuilder("Default");

        var mesh = new MeshBuilder<VertexPositionNormal, VertexEmpty, VertexJoints4>("Tri");
        var prim = mesh.UsePrimitive(material);

        var v0 = new VertexBuilder<VertexPositionNormal, VertexEmpty, VertexJoints4>(
            new VertexPositionNormal(new Vector3(0, 0, 0), Vector3.UnitZ),
            (0, 1f));

        var v1 = new VertexBuilder<VertexPositionNormal, VertexEmpty, VertexJoints4>(
            new VertexPositionNormal(new Vector3(1, 0, 0), Vector3.UnitZ),
            (0, 1f));

        var v2 = new VertexBuilder<VertexPositionNormal, VertexEmpty, VertexJoints4>(
            new VertexPositionNormal(new Vector3(0, 1, 0), Vector3.UnitZ),
            (0, 1f));

        prim.AddTriangle(v0, v1, v2);

        var scene = new SceneBuilder();
        scene.AddRigidMesh(mesh, Matrix4x4.Identity);

        var model = scene.ToGltf2();
        using var ms = new MemoryStream();
        model.WriteGLB(ms);
        return ms.ToArray();
    }

    private static byte[] CreateTriangleGltfJson()
    {
        const string json = """
        {
          "asset": { "version": "2.0", "generator": "Oxygen.Assets.Tests" },
          "buffers": [ { "uri": "buffer0.bin", "byteLength": 84 } ],
          "bufferViews": [
            { "buffer": 0, "byteOffset": 0,  "byteLength": 36, "target": 34962 },
            { "buffer": 0, "byteOffset": 36, "byteLength": 36, "target": 34962 },
            { "buffer": 0, "byteOffset": 72, "byteLength": 12, "target": 34963 }
          ],
          "accessors": [
            { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
            { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
            { "bufferView": 2, "componentType": 5125, "count": 3, "type": "SCALAR" }
          ],
          "meshes": [
            { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 2 } ] }
          ],
          "nodes": [ { "mesh": 0 } ],
          "scenes": [ { "nodes": [0] } ],
          "scene": 0
        }
        """;

        return System.Text.Encoding.UTF8.GetBytes(json);
    }

    private static byte[] CreateTriangleBin()
    {
        // Layout: positions (36 bytes), normals (36 bytes), indices (12 bytes).
        var bin = new byte[84];
        var span = bin.AsSpan();

        WriteVec3(span, 0, new Vector3(0, 0, 0));
        WriteVec3(span, 12, new Vector3(1, 0, 0));
        WriteVec3(span, 24, new Vector3(0, 1, 0));

        WriteVec3(span, 36, Vector3.UnitZ);
        WriteVec3(span, 48, Vector3.UnitZ);
        WriteVec3(span, 60, Vector3.UnitZ);

        WriteU32(span, 72, 0);
        WriteU32(span, 76, 1);
        WriteU32(span, 80, 2);

        return bin;
    }

    private static void WriteU32(Span<byte> destination, int offset, uint value)
    {
        BitConverter.GetBytes(value).CopyTo(destination.Slice(offset, 4));
    }

    private static void WriteF32(Span<byte> destination, int offset, float value)
    {
        BitConverter.GetBytes(value).CopyTo(destination.Slice(offset, 4));
    }

    private static void WriteVec3(Span<byte> destination, int offset, Vector3 value)
    {
        WriteF32(destination, offset + 0, value.X);
        WriteF32(destination, offset + 4, value.Y);
        WriteF32(destination, offset + 8, value.Z);
    }

    private sealed class FixedIdentityPolicy(AssetKey key) : IAssetIdentityPolicy
    {
        public AssetKey GetOrCreateAssetKey(string virtualPath, string assetType)
        {
            _ = virtualPath;
            _ = assetType;
            return key;
        }
    }

    private sealed class InMemoryImportFileAccess : IImportFileAccess
    {
        private readonly ConcurrentDictionary<string, Entry> files = new(StringComparer.Ordinal);

        public void AddBytes(string relativePath, byte[] bytes)
        {
            ArgumentNullException.ThrowIfNull(relativePath);
            ArgumentNullException.ThrowIfNull(bytes);
            this.files[relativePath] = new Entry(bytes, DateTimeOffset.UtcNow);
        }

        public bool TryGet(string relativePath, out byte[] bytes)
        {
            if (this.files.TryGetValue(relativePath, out var entry))
            {
                bytes = entry.Bytes;
                return true;
            }

            bytes = [];
            return false;
        }

        public ValueTask<ImportFileMetadata> GetMetadataAsync(string sourcePath, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();

            return !this.files.TryGetValue(sourcePath, out var entry)
                ? throw new FileNotFoundException("Missing file.", sourcePath)
                : ValueTask.FromResult(new ImportFileMetadata(
                    ByteLength: entry.Bytes.Length,
                    LastWriteTimeUtc: entry.LastWriteTimeUtc));
        }

        public ValueTask<ReadOnlyMemory<byte>> ReadHeaderAsync(string sourcePath, int maxBytes, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();

            if (!this.files.TryGetValue(sourcePath, out var entry))
            {
                throw new FileNotFoundException("Missing file.", sourcePath);
            }

            var len = Math.Min(maxBytes, entry.Bytes.Length);
            return ValueTask.FromResult<ReadOnlyMemory<byte>>(entry.Bytes.AsMemory(0, len));
        }

        public ValueTask<ReadOnlyMemory<byte>> ReadAllBytesAsync(string sourcePath, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();

            return !this.files.TryGetValue(sourcePath, out var entry)
                ? throw new FileNotFoundException("Missing file.", sourcePath)
                : ValueTask.FromResult<ReadOnlyMemory<byte>>(entry.Bytes);
        }

        public ValueTask WriteAllBytesAsync(string relativePath, ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            this.files[relativePath] = new Entry(bytes.ToArray(), DateTimeOffset.UtcNow);
            return ValueTask.CompletedTask;
        }

        private sealed record Entry(byte[] Bytes, DateTimeOffset LastWriteTimeUtc);
    }
}
