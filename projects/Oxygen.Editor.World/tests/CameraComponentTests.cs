// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Tests;

[TestClass]
public class CameraComponentTests
{
    [TestMethod]
    public void PerspectiveCamera_Defaults_ShouldUseEditorDegreeUnits()
    {
        var cam = new PerspectiveCamera { Name = "Camera" };
        var data = new PerspectiveCameraData { Name = "Camera" };

        _ = cam.FieldOfView.Should().Be(60f);
        _ = cam.AspectRatio.Should().Be(16f / 9f);
        _ = data.FieldOfView.Should().Be(60f);
        _ = data.AspectRatio.Should().Be(16f / 9f);
    }

    [TestMethod]
    public void PerspectiveCamera_Hydrate_Dehydrate_RoundTrip()
    {
        var cam = new PerspectiveCamera { Name = "MainCam", NearPlane = 0.3f, FarPlane = 500f, FieldOfView = 45f, AspectRatio = 4f / 3f, ApertureF = 2.8f, ShutterRate = 250f, Iso = 400f };

        var dto = cam.Dehydrate();

        _ = dto.Should().BeOfType<PerspectiveCameraData>();
        var pd = (PerspectiveCameraData)dto;
        _ = pd.Name.Should().Be("MainCam");
        _ = pd.NearPlane.Should().Be(0.3f);
        _ = pd.FarPlane.Should().Be(500f);
        _ = pd.FieldOfView.Should().Be(45f);
        _ = pd.AspectRatio.Should().Be(4f / 3f);

        var recreated = GameComponent.CreateAndHydrate(pd) as PerspectiveCamera;
        _ = recreated.Should().NotBeNull();
        _ = recreated!.Name.Should().Be("MainCam");
        _ = recreated.NearPlane.Should().Be(0.3f);
        _ = recreated.FarPlane.Should().Be(500f);
        _ = recreated.FieldOfView.Should().Be(45f);
        _ = recreated.AspectRatio.Should().Be(4f / 3f);
        _ = recreated.ApertureF.Should().Be(2.8f);
        _ = recreated.ShutterRate.Should().Be(250f);
        _ = recreated.Iso.Should().Be(400f);
    }

    [TestMethod]
    public void OrthographicCamera_Hydrate_Dehydrate_RoundTrip()
    {
        var cam = new OrthographicCamera { Name = "OrthoCam", NearPlane = 0.5f, FarPlane = 200f, OrthographicSize = 20f, ApertureF = 8f, ShutterRate = 60f, Iso = 200f };

        var dto = cam.Dehydrate();

        _ = dto.Should().BeOfType<OrthographicCameraData>();
        var od = (OrthographicCameraData)dto;
        _ = od.Name.Should().Be("OrthoCam");
        _ = od.NearPlane.Should().Be(0.5f);
        _ = od.FarPlane.Should().Be(200f);
        _ = od.OrthographicSize.Should().Be(20f);

        var recreated = GameComponent.CreateAndHydrate(od) as OrthographicCamera;
        _ = recreated.Should().NotBeNull();
        _ = recreated!.Name.Should().Be("OrthoCam");
        _ = recreated.OrthographicSize.Should().Be(20f);
        _ = recreated.ApertureF.Should().Be(8f);
        _ = recreated.ShutterRate.Should().Be(60f);
        _ = recreated.Iso.Should().Be(200f);
    }
    [TestMethod]
    [DataRow(0f)]
    [DataRow(-1f)]
    [DataRow(float.NaN)]
    [DataRow(float.PositiveInfinity)]
    public void PhysicalExposure_RejectsInvalidValuesWithoutMutation(float invalid)
    {
        foreach (CameraComponent camera in new CameraComponent[] { new PerspectiveCamera { Name = "Perspective" }, new OrthographicCamera { Name = "Ortho" } })
        {
            Action changeAperture = () => camera.ApertureF = invalid;
            Action changeShutter = () => camera.ShutterRate = invalid;
            Action changeIso = () => camera.Iso = invalid;
            _ = changeAperture.Should().Throw<ArgumentOutOfRangeException>();
            _ = changeShutter.Should().Throw<ArgumentOutOfRangeException>();
            _ = changeIso.Should().Throw<ArgumentOutOfRangeException>();
            _ = camera.ApertureF.Should().Be(11f);
            _ = camera.ShutterRate.Should().Be(125f);
            _ = camera.Iso.Should().Be(100f);
        }
    }
}
