// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.SceneInspector.Components.Cameras;
using Oxygen.Editor.SceneInspector.Components.Lights;
using Oxygen.Editor.SceneInspector.Components.Rendering;
using Oxygen.Editor.SceneInspector.Components.Transform;
using Oxygen.Editor.SceneInspector.SceneSettings;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector.TestSupport;

internal static partial class InspectorModels
{
    internal static IDisposable CreateModel(string kind, SceneAuthoringFixture fixture)
    {
        switch (kind)
        {
            case "Transform":
                var transform = new TransformViewModel(authoringProvider: () => fixture.Authoring)
                {
                    IsExpanded = true,
                };
                transform.UpdateValues([fixture.Node]);
                return transform;
            case "Camera":
                var camera = new PerspectiveCameraViewModel(() => fixture.Authoring)
                {
                    IsExpanded = true,
                };
                camera.UpdateValues([fixture.Node]);
                return camera;
            case "Light":
                var light = new DirectionalLightViewModel(() => fixture.Authoring)
                {
                    IsExpanded = true,
                };
                light.UpdateValues([fixture.Node]);
                return light;
            case "Environment":
                var environment = new EnvironmentViewModel(() => fixture.Authoring)
                {
                    IsExpanded = true,
                };
                environment.SetScene(fixture.Scene);
                return environment;
            case "OrthographicCamera":
                ReplaceComponent<Oxygen.Editor.World.Components.CameraComponent>(fixture, new OrthographicCamera { Name = "Orthographic Camera", AspectMode = Oxygen.Managed.Core.CameraAspectMode.Auto });
                var orthographic = new OrthographicCameraViewModel(() => fixture.Authoring) { IsExpanded = true };
                orthographic.UpdateValues([fixture.Node]);
                return orthographic;
            case "PointLight":
                ReplaceComponent<LightComponent>(fixture, new PointLightComponent { Name = "Point Light", Range = 10f });
                var point = new PointLightViewModel(() => fixture.Authoring) { IsExpanded = true };
                point.UpdateValues([fixture.Node]);
                return point;
            case "SpotLight":
                ReplaceComponent<LightComponent>(fixture, new SpotLightComponent { Name = "Spot Light", Range = 10f, InnerConeAngleRadians = 0.3f, OuterConeAngleRadians = 0.5f });
                var spot = new SpotLightViewModel(() => fixture.Authoring) { IsExpanded = true };
                spot.UpdateValues([fixture.Node]);
                return spot;
            case "Rendering":
                var rendering = new NodeRenderingViewModel(() => fixture.Authoring) { IsExpanded = true };
                rendering.UpdateValues([fixture.Node]);
                return rendering;
            default:
                throw new ArgumentOutOfRangeException(nameof(kind));
        }
    }

    private static void ReplaceComponent<TBase>(SceneAuthoringFixture fixture, GameComponent replacement)
        where TBase : GameComponent
    {
        foreach (var existing in fixture.Node.Components.OfType<TBase>().ToArray())
        {
            _ = fixture.Node.Components.Remove(existing);
        }

        fixture.Node.Components.Add(replacement);
    }
}
