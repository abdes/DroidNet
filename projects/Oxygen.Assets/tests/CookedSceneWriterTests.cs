// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Assets.Cook;
using Oxygen.Assets.Import.Scenes;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class CookedSceneWriterTests
{
    private const uint ComponentTypePerspectiveCamera = 0x4D414350; // 'PCAM'
    private const int SceneDirectoryOffsetOffset = 127;
    private const int SceneDirectoryCountOffset = 135;
    private const int ComponentTableDescSize = 20;

    [TestMethod]
    public void Write_ShouldConvertPerspectiveCameraFovDegreesToRadians()
    {
        var scene = new SceneSource(
            "oxygen.scene.v1",
            "Scene",
            [
                new SceneNodeSource("Camera", null, null, null, null, null)
                {
                    PerspectiveCamera = new PerspectiveCameraSource(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f),
                },
            ]);

        using var stream = new MemoryStream();
        CookedSceneWriter.Write(stream, scene, static _ => null);
        var bytes = stream.ToArray();

        var cameraRecordOffset = FindComponentTable(bytes, ComponentTypePerspectiveCamera);
        _ = BitConverter.ToUInt32(bytes, cameraRecordOffset).Should().Be(0);
        _ = BitConverter.ToSingle(bytes, cameraRecordOffset + 4).Should().BeApproximately(MathF.PI / 3.0f, 0.00001f);
        _ = BitConverter.ToSingle(bytes, cameraRecordOffset + 8).Should().BeApproximately(16.0f / 9.0f, 0.00001f);
        _ = BitConverter.ToSingle(bytes, cameraRecordOffset + 12).Should().BeApproximately(0.1f, 0.00001f);
        _ = BitConverter.ToSingle(bytes, cameraRecordOffset + 16).Should().BeApproximately(1000.0f, 0.00001f);
    }

    [TestMethod]
    public void Write_ShouldEmitDefaultAtmosphereAndPostProcessEnvironment()
    {
        var scene = new SceneSource("oxygen.scene.v1", "Scene", []);

        using var stream = new MemoryStream();
        CookedSceneWriter.Write(stream, scene, static _ => null);
        var bytes = stream.ToArray();

        var environmentOffset = bytes.Length - 236;
        _ = environmentOffset.Should().BeGreaterThan(0);
        _ = BitConverter.ToUInt32(bytes, environmentOffset).Should().Be(236);
        _ = BitConverter.ToUInt32(bytes, environmentOffset + 4).Should().Be(2);

        var skyAtmosphereOffset = environmentOffset + 8;
        _ = BitConverter.ToUInt32(bytes, skyAtmosphereOffset).Should().Be(0);
        _ = BitConverter.ToUInt32(bytes, skyAtmosphereOffset + 4).Should().Be(168);
        _ = BitConverter.ToUInt32(bytes, skyAtmosphereOffset + 8).Should().Be(1);

        var postProcessOffset = skyAtmosphereOffset + 168;
        _ = BitConverter.ToUInt32(bytes, postProcessOffset).Should().Be(5);
        _ = BitConverter.ToUInt32(bytes, postProcessOffset + 4).Should().Be(60);
        _ = BitConverter.ToUInt32(bytes, postProcessOffset + 8).Should().Be(1);
        _ = BitConverter.ToUInt32(bytes, postProcessOffset + 12).Should().Be(1);
        _ = BitConverter.ToUInt32(bytes, postProcessOffset + 16).Should().Be(2);
    }

    private static int FindComponentTable(byte[] bytes, uint componentType)
    {
        var directoryOffset = checked((int)BitConverter.ToUInt64(bytes, SceneDirectoryOffsetOffset));
        var directoryCount = checked((int)BitConverter.ToUInt32(bytes, SceneDirectoryCountOffset));

        for (var i = 0; i < directoryCount; i++)
        {
            var descriptorOffset = directoryOffset + (i * ComponentTableDescSize);
            if (BitConverter.ToUInt32(bytes, descriptorOffset) != componentType)
            {
                continue;
            }

            _ = BitConverter.ToUInt32(bytes, descriptorOffset + 12).Should().Be(1);
            _ = BitConverter.ToUInt32(bytes, descriptorOffset + 16).Should().Be(20);
            return checked((int)BitConverter.ToUInt64(bytes, descriptorOffset + 4));
        }

        Assert.Fail($"Component table 0x{componentType:X8} was not found.");
        return 0;
    }
}
