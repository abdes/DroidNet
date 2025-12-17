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
    public void PerspectiveCamera_Hydrate_Dehydrate_RoundTrip()
    {
        var cam = new PerspectiveCamera { Name = "MainCam", NearPlane = 0.3f, FarPlane = 500f, FieldOfView = 45f, AspectRatio = 4f / 3f };

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
    }

    [TestMethod]
    public void OrthographicCamera_Hydrate_Dehydrate_RoundTrip()
    {
        var cam = new OrthographicCamera { Name = "OrthoCam", NearPlane = 0.5f, FarPlane = 200f, OrthographicSize = 20f };

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
    }
}
