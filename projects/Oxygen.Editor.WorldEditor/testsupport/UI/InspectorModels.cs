// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Model;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class InspectorModels
{
    internal static IDisposable CreateModel(string kind, SceneAuthoringFixture fixture)
    {
        switch (kind)
        {
            case "Transform":
                var transform = new TransformViewModel(commandService: fixture.Commands, commandContextProvider: () => fixture.Context)
                {
                    IsExpanded = true,
                };
                transform.UpdateValues([fixture.Node]);
                return transform;
            case "Camera":
                var camera = new PerspectiveCameraViewModel(fixture.Commands, () => fixture.Context)
                {
                    IsExpanded = true,
                };
                camera.UpdateValues([fixture.Node]);
                return camera;
            case "Light":
                var light = new DirectionalLightViewModel(fixture.Commands, () => fixture.Context)
                {
                    IsExpanded = true,
                };
                light.UpdateValues([fixture.Node]);
                return light;
            case "Environment":
                var environment = new EnvironmentViewModel(fixture.Commands, () => fixture.Context)
                {
                    IsExpanded = true,
                };
                environment.SetScene(fixture.Scene);
                return environment;
            case "OrthographicCamera":
                ReplaceComponent<Oxygen.Editor.World.Components.CameraComponent>(fixture, new OrthographicCamera { Name = "Orthographic Camera", AspectMode = Oxygen.Managed.Core.CameraAspectMode.Auto });
                var orthographic = new OrthographicCameraViewModel(fixture.Commands, () => fixture.Context) { IsExpanded = true };
                orthographic.UpdateValues([fixture.Node]);
                return orthographic;
            case "PointLight":
                ReplaceComponent<LightComponent>(fixture, new PointLightComponent { Name = "Point Light", Range = 10f });
                var point = new PointLightViewModel(fixture.Commands, () => fixture.Context) { IsExpanded = true };
                point.UpdateValues([fixture.Node]);
                return point;
            case "SpotLight":
                ReplaceComponent<LightComponent>(fixture, new SpotLightComponent { Name = "Spot Light", Range = 10f, InnerConeAngleRadians = 0.3f, OuterConeAngleRadians = 0.5f });
                var spot = new SpotLightViewModel(fixture.Commands, () => fixture.Context) { IsExpanded = true };
                spot.UpdateValues([fixture.Node]);
                return spot;
            case "Rendering":
                var rendering = new NodeRenderingViewModel(fixture.Commands, () => fixture.Context) { IsExpanded = true };
                rendering.UpdateValues([fixture.Node]);
                return rendering;
            case "Material":
                return CreateMaterialEditor();
            default:
                throw new ArgumentOutOfRangeException(nameof(kind));
        }
    }

    internal static MaterialEditorViewModel CreateMaterialEditor(Oxygen.Editor.ContentBrowser.AssetIdentity.IContentBrowserAssetProvider? assetProvider = null)
    {
        var uri = new Uri("asset:///Content/Materials/UI.omat.json");
        var source = new MaterialSource(name: "UI", pbrMetallicRoughness: new MaterialPbrMetallicRoughness(1, 1, 1, 1, 0, 0.5f, baseColorTexture: null, metallicRoughnessTexture: null), normalTexture: null, occlusionTexture: null, alphaMode: MaterialAlphaMode.Opaque, alphaCutoff: 0.5f, doubleSided: false);
        var document = new MaterialDocument(Guid.NewGuid(), uri, Guid.NewGuid(), "C:/UI.omat.json", "UI", source, new MaterialAsset { Uri = uri, Source = source }, IsDirty: false, MaterialCookState.NotCooked);
        var service = new Mock<IMaterialDocumentService>();
        _ = service.Setup(value => value.OpenAsync(uri, It.IsAny<CancellationToken>())).ReturnsAsync(document);
        _ = service.Setup(value => value.GetDocument(document.DocumentId)).Returns(document);
        return new(new MaterialDocumentMetadata(uri), service.Object, assetProvider ?? Oxygen.Testing.AssetStatusFixture.EmptyProvider, CreateStatusHosting().DispatcherScheduler);
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
