// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies camera component defaults, round-trips, and physical exposure validation.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(CameraComponent))]
public sealed class CameraComponentTests
{
    [TestMethod]
    public void PerspectiveCamera_Defaults_ComeFromTheSceneSchema()
    {
        var cam = new PerspectiveCamera { Name = "Camera" };
        var data = new PerspectiveCameraData { Name = "Camera", AspectMode = SceneSchema.PerspectiveCamera.Defaults.AspectMode };

        _ = cam.FieldOfView.Should().Be(SceneSchema.PerspectiveCamera.Defaults.FieldOfViewDegrees).And.Be(data.FieldOfView);
        _ = cam.AspectRatio.Should().Be(SceneSchema.PerspectiveCamera.Defaults.AspectRatio).And.Be(data.AspectRatio);
        _ = cam.NearPlane.Should().Be(SceneSchema.PerspectiveCamera.Defaults.NearPlane).And.Be(data.NearPlane);
        _ = cam.FarPlane.Should().Be(SceneSchema.PerspectiveCamera.Defaults.FarPlane).And.Be(data.FarPlane);
        _ = cam.ApertureF.Should().Be(SceneSchema.PerspectiveCamera.Defaults.ApertureF).And.Be(data.ApertureF);
        _ = cam.ShutterRate.Should().Be(SceneSchema.PerspectiveCamera.Defaults.ShutterRate).And.Be(data.ShutterRate);
        _ = cam.Iso.Should().Be(SceneSchema.PerspectiveCamera.Defaults.Iso).And.Be(data.Iso);
        _ = cam.AspectMode.Should().Be(SceneSchema.PerspectiveCamera.Defaults.AspectMode);
    }

    [TestMethod]
    public void OrthographicCamera_Defaults_ComeFromTheSceneSchema()
    {
        var cam = new OrthographicCamera { Name = "Camera" };
        var data = new OrthographicCameraData { Name = "Camera", AspectMode = SceneSchema.OrthographicCamera.Defaults.AspectMode };

        _ = cam.OrthographicSize.Should().Be(SceneSchema.OrthographicCamera.Defaults.OrthographicSize).And.Be(data.OrthographicSize);
        _ = cam.AspectRatio.Should().Be(SceneSchema.OrthographicCamera.Defaults.AspectRatio).And.Be(data.AspectRatio);
        _ = cam.NearPlane.Should().Be(SceneSchema.OrthographicCamera.Defaults.NearPlane).And.Be(data.NearPlane);
        _ = cam.FarPlane.Should().Be(SceneSchema.OrthographicCamera.Defaults.FarPlane).And.Be(data.FarPlane);
        _ = cam.ApertureF.Should().Be(SceneSchema.OrthographicCamera.Defaults.ApertureF).And.Be(data.ApertureF);
        _ = cam.ShutterRate.Should().Be(SceneSchema.OrthographicCamera.Defaults.ShutterRate).And.Be(data.ShutterRate);
        _ = cam.Iso.Should().Be(SceneSchema.OrthographicCamera.Defaults.Iso).And.Be(data.Iso);
        _ = cam.AspectMode.Should().Be(SceneSchema.OrthographicCamera.Defaults.AspectMode);
    }

    [TestMethod]
    public void PerspectiveCamera_Hydrate_Dehydrate_RoundTrip()
    {
        var cam = new PerspectiveCamera { Name = "MainCam", NearPlane = 0.3f, FarPlane = 500f, FieldOfView = 45f, AspectRatio = 4f / 3f, AspectMode = CameraAspectMode.Fixed, ApertureF = 2.8f, ShutterRate = 250f, Iso = 400f };

        var dto = cam.Dehydrate();

        _ = dto.Should().BeOfType<PerspectiveCameraData>();
        var pd = (PerspectiveCameraData)dto;
        _ = pd.Name.Should().Be("MainCam");
        _ = pd.NearPlane.Should().Be(0.3f);
        _ = pd.FarPlane.Should().Be(500f);
        _ = pd.FieldOfView.Should().Be(45f);
        _ = pd.AspectRatio.Should().Be(4f / 3f);
        _ = pd.AspectMode.Should().Be(CameraAspectMode.Fixed);

        var recreated = GameComponent.CreateAndHydrate(pd) as PerspectiveCamera;
        _ = recreated.Should().NotBeNull();
        _ = recreated!.Name.Should().Be("MainCam");
        _ = recreated.NearPlane.Should().Be(0.3f);
        _ = recreated.FarPlane.Should().Be(500f);
        _ = recreated.FieldOfView.Should().Be(45f);
        _ = recreated.AspectRatio.Should().Be(4f / 3f);
        _ = recreated.AspectMode.Should().Be(CameraAspectMode.Fixed);
        _ = recreated.ApertureF.Should().Be(2.8f);
        _ = recreated.ShutterRate.Should().Be(250f);
        _ = recreated.Iso.Should().Be(400f);
    }

    [TestMethod]
    public void OrthographicCamera_Hydrate_Dehydrate_RoundTrip()
    {
        var cam = new OrthographicCamera { Name = "OrthoCam", NearPlane = 0.5f, FarPlane = 200f, OrthographicSize = 20f, AspectRatio = 4f / 3f, AspectMode = CameraAspectMode.Fixed, ApertureF = 8f, ShutterRate = 60f, Iso = 200f };

        var dto = cam.Dehydrate();

        _ = dto.Should().BeOfType<OrthographicCameraData>();
        var od = (OrthographicCameraData)dto;
        _ = od.Name.Should().Be("OrthoCam");
        _ = od.NearPlane.Should().Be(0.5f);
        _ = od.FarPlane.Should().Be(200f);
        _ = od.OrthographicSize.Should().Be(20f);
        _ = od.AspectRatio.Should().Be(4f / 3f);
        _ = od.AspectMode.Should().Be(CameraAspectMode.Fixed);

        var recreated = GameComponent.CreateAndHydrate(od) as OrthographicCamera;
        _ = recreated.Should().NotBeNull();
        _ = recreated!.Name.Should().Be("OrthoCam");
        _ = recreated.OrthographicSize.Should().Be(20f);
        _ = recreated.AspectRatio.Should().Be(4f / 3f);
        _ = recreated.AspectMode.Should().Be(CameraAspectMode.Fixed);
        _ = recreated.ApertureF.Should().Be(8f);
        _ = recreated.ShutterRate.Should().Be(60f);
        _ = recreated.Iso.Should().Be(200f);
    }

    [TestMethod]
    [DataRow(0f)]
    [DataRow(-1f)]
    [DataRow(float.NaN)]
    [DataRow(float.PositiveInfinity)]
    public void CameraProperties_RejectValuesOutsideTheSchemaRangeWithoutMutation(float invalid)
    {
        foreach (var camera in new CameraComponent[] { new PerspectiveCamera { Name = "Perspective" }, new OrthographicCamera { Name = "Ortho" } })
        {
            var before = (camera.NearPlane, camera.FarPlane, camera.ApertureF, camera.ShutterRate, camera.Iso);
            Action changeNear = () => camera.NearPlane = invalid;
            Action changeFar = () => camera.FarPlane = invalid;
            Action changeAperture = () => camera.ApertureF = invalid;
            Action changeShutter = () => camera.ShutterRate = invalid;
            Action changeIso = () => camera.Iso = invalid;
            _ = changeNear.Should().Throw<ArgumentOutOfRangeException>();
            _ = changeFar.Should().Throw<ArgumentOutOfRangeException>();
            _ = changeAperture.Should().Throw<ArgumentOutOfRangeException>();
            _ = changeShutter.Should().Throw<ArgumentOutOfRangeException>();
            _ = changeIso.Should().Throw<ArgumentOutOfRangeException>();
            _ = (camera.NearPlane, camera.FarPlane, camera.ApertureF, camera.ShutterRate, camera.Iso).Should().Be(before);
        }
    }

    [TestMethod]
    [DataRow(0f)]
    [DataRow(0.5f)]
    [DataRow(179.5f)]
    [DataRow(float.NaN)]
    public void PerspectiveFieldOfView_OutsideTheSchemaRange_IsRejectedWithoutMutation(float invalid)
    {
        var camera = new PerspectiveCamera { Name = "Camera" };

        Action change = () => camera.FieldOfView = invalid;
        Action hydrate = () => camera.Hydrate(new PerspectiveCameraData { Name = "Rejected", AspectMode = CameraAspectMode.Auto, FieldOfView = invalid });

        _ = change.Should().Throw<ArgumentOutOfRangeException>().Which.ActualValue.Should().Be(invalid);
        _ = hydrate.Should().Throw<ArgumentOutOfRangeException>();
        _ = camera.FieldOfView.Should().Be(SceneSchema.PerspectiveCamera.Defaults.FieldOfViewDegrees);
        _ = camera.Name.Should().Be("Camera");
    }

    [TestMethod]
    [DataRow(0f)]
    [DataRow(-1f)]
    [DataRow(float.PositiveInfinity)]
    public void AspectRatioAndOrthographicSize_OutsideTheSchemaRange_AreRejectedWithoutMutation(float invalid)
    {
        var perspective = new PerspectiveCamera { Name = "Perspective" };
        var orthographic = new OrthographicCamera { Name = "Orthographic" };

        Action perspectiveAspect = () => perspective.AspectRatio = invalid;
        Action orthographicAspect = () => orthographic.AspectRatio = invalid;
        Action orthographicSize = () => orthographic.OrthographicSize = invalid;
        Action hydrateSize = () => orthographic.Hydrate(new OrthographicCameraData { Name = "Rejected", AspectMode = CameraAspectMode.Auto, OrthographicSize = invalid });
        Action hydrateAspect = () => perspective.Hydrate(new PerspectiveCameraData { Name = "Rejected", AspectMode = CameraAspectMode.Auto, AspectRatio = invalid });

        _ = perspectiveAspect.Should().Throw<ArgumentOutOfRangeException>();
        _ = orthographicAspect.Should().Throw<ArgumentOutOfRangeException>();
        _ = orthographicSize.Should().Throw<ArgumentOutOfRangeException>();
        _ = hydrateSize.Should().Throw<ArgumentOutOfRangeException>();
        _ = hydrateAspect.Should().Throw<ArgumentOutOfRangeException>();
        _ = perspective.AspectRatio.Should().Be(SceneSchema.PerspectiveCamera.Defaults.AspectRatio);
        _ = orthographic.AspectRatio.Should().Be(SceneSchema.OrthographicCamera.Defaults.AspectRatio);
        _ = orthographic.OrthographicSize.Should().Be(SceneSchema.OrthographicCamera.Defaults.OrthographicSize);
        _ = (perspective.Name, orthographic.Name).Should().Be(("Perspective", "Orthographic"));
    }

    [TestMethod]
    public void AspectMode_Undefined_IsRejected()
    {
        var perspective = new PerspectiveCamera { Name = "Perspective" };
        var orthographic = new OrthographicCamera { Name = "Orthographic" };

        Action perspectiveMode = () => perspective.AspectMode = (CameraAspectMode)99;
        Action orthographicMode = () => orthographic.Hydrate(new OrthographicCameraData { Name = "Rejected", AspectMode = (CameraAspectMode)99 });

        _ = perspectiveMode.Should().Throw<ArgumentOutOfRangeException>().Which.ActualValue.Should().Be((CameraAspectMode)99);
        _ = orthographicMode.Should().Throw<ArgumentOutOfRangeException>();
        _ = orthographic.Name.Should().Be("Orthographic");
    }

    [TestMethod]
    public void Hydrate_RejectsDataOutsideTheSchemaRangeWithoutMutation()
    {
        var camera = new PerspectiveCamera { Name = "Camera" };

        Action hydrate = () => camera.Hydrate(new PerspectiveCameraData { Name = "Rejected", AspectMode = CameraAspectMode.Auto, NearPlane = 0f });

        _ = hydrate.Should().Throw<ArgumentOutOfRangeException>();
        _ = camera.Name.Should().Be("Camera");
        _ = camera.NearPlane.Should().Be(SceneSchema.PerspectiveCamera.Defaults.NearPlane);
    }
}
