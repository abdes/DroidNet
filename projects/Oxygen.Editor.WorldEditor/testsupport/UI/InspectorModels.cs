// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Tests;
using DroidNet.TimeMachine;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Diagnostics;
using static DroidNet.Tests.UiTestHosting;
using NumberBox = DroidNet.Controls.NumberBox;

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
                    IsExpanded = true
                };
                transform.UpdateValues([fixture.Node]);
                return transform;
            case "Camera":
                var camera = new PerspectiveCameraViewModel(fixture.Commands, () => fixture.Context)
                {
                    IsExpanded = true
                };
                camera.UpdateValues([fixture.Node]);
                return camera;
            case "Light":
                var light = new DirectionalLightViewModel(fixture.Commands, () => fixture.Context)
                {
                    IsExpanded = true
                };
                light.UpdateValues([fixture.Node]);
                return light;
            case "Environment":
                var environment = new EnvironmentViewModel(fixture.Commands, () => fixture.Context)
                {
                    IsExpanded = true
                };
                environment.SetScene(fixture.Scene);
                return environment;
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
}
