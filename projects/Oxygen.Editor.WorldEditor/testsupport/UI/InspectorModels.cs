// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Security.Cryptography;
using System.Text;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Materials;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.WorldEditor.TestSupport;

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
            case "Material":
                return CreateMaterialEditor();
            default:
                throw new ArgumentOutOfRangeException(nameof(kind));
        }
    }

    /// <summary>Opens an in-memory material in the material editor.</summary>
    /// <param name="assetProvider">The asset feed the editor reads, or one that never changes.</param>
    /// <returns>The editor, with the open documents and tab it shows, all disposed together.</returns>
    internal static MaterialEditorModel CreateMaterialEditor(IAssetFeed? assetProvider = null)
    {
        // The file is held in memory, so the material opens at once.
        var validation = new MaterialValidation();
        var kind = new MaterialDocumentKind(validation);
        var files = new MemoryFiles(Encoding.UTF8.GetBytes(MaterialTemplates.New("UI")));
        var documents = new OpenDocuments([kind], files);
        try
        {
            var opening = documents.OpenAsync(new DocumentFile(FileSystemPath.From("C:/UI/Content/Materials/UI.omat.json"), AssetPath.Parse("/Content/Materials/UI.omat")));
            if (!opening.IsCompletedSuccessfully || opening.Result.Document is not { } document)
            {
                throw new InvalidOperationException("An in-memory material opens at once.");
            }

            return new MaterialEditorModel(documents, new DocumentTab(document, new DocumentEditorDescriptor(kind, static _ => new object())), validation, assetProvider);
        }
        catch
        {
            documents.Dispose();
            throw;
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

    /// <summary>A material editor over an in-memory material, owning the open documents and the tab it shows.</summary>
    /// <param name="documents">The open documents that hold the material.</param>
    /// <param name="tab">The material's tab.</param>
    /// <param name="validation">The material rules the edits check.</param>
    /// <param name="assetProvider">The asset feed the editor reads, or one that never changes.</param>
    internal sealed partial class MaterialEditorModel(OpenDocuments documents, DocumentTab tab, MaterialValidation validation, IAssetFeed? assetProvider) : IDisposable
    {
        /// <summary>Gets the material editor.</summary>
        public MaterialEditorViewModel Editor { get; } = new(
            tab,
            new MaterialEdits(validation),
            assetProvider ?? Mock.Of<IAssetFeed>(feed => feed.Items == Observable.Never<IReadOnlyList<AssetEntry>>()),
            CreateStatusHosting().DispatcherScheduler,
            Mock.Of<IMaterialCookService>(),
            Mock.Of<IProjectContextService>(),
            new MockFileSystem());

        /// <inheritdoc/>
        public void Dispose()
        {
            this.Editor.Dispose();
            tab.Dispose();
            documents.Dispose();
        }
    }

    /// <summary>Holds one file's bytes in memory, whatever its path.</summary>
    private sealed class MemoryFiles(byte[] initial) : IAtomicFileStore
    {
        private byte[] content = initial;

        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
            => Task.FromResult(new FileSnapshot([.. this.content], VersionOf(this.content)));

        public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> written, FileVersion expected, CancellationToken cancellationToken = default)
        {
            this.content = written.ToArray();
            return Task.FromResult(VersionOf(this.content));
        }

        private static FileVersion VersionOf(byte[] bytes) => new(Exists: true, Convert.ToHexString(SHA256.HashData(bytes)));
    }
}
