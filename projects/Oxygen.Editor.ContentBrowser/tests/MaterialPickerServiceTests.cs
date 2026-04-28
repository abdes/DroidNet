// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Catalog;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Model;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using CommunityToolkit.Mvvm.Messaging;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class MaterialPickerServiceTests
{
    [TestMethod]
    public async Task RefreshAsync_WhenSourceDescriptorExists_ShouldReturnSourceWithBaseColorPreview()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Wood.omat.json"), 0.2f, 0.3f, 0.4f, 1.0f);
        var project = CreateProjectContext(workspace.Root);
        var context = new ProjectContextService();
        context.Activate(project);
        var catalog = new TestAssetCatalog([new AssetRecord(new Uri("asset:///Content/Materials/Wood.omat.json"))]);
        var service = new MaterialPickerService(catalog, context);

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false });

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(AssetState.Source, rows[0].State);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Wood.omat.json"), rows[0].MaterialUri);
        Assert.IsNotNull(rows[0].BaseColorPreview);
        Assert.AreEqual(0.2f, rows[0].BaseColorPreview!.R);
    }

    [TestMethod]
    public async Task RefreshAsync_WhenSourceIsNewerThanCooked_ShouldReturnStaleAuthoredRow()
    {
        using var workspace = new TempWorkspace();
        var sourcePath = workspace.SourcePath("Content/Materials/Wood.omat.json");
        var cookedPath = workspace.SourcePath(".cooked/Content/Materials/Wood.omat");
        WriteMaterial(sourcePath, 1.0f, 1.0f, 1.0f, 1.0f);
        Directory.CreateDirectory(Path.GetDirectoryName(cookedPath)!);
        await File.WriteAllBytesAsync(cookedPath, new byte[] { 1 });
        File.SetLastWriteTimeUtc(cookedPath, DateTime.UtcNow.AddMinutes(-5));
        File.SetLastWriteTimeUtc(sourcePath, DateTime.UtcNow);

        var context = new ProjectContextService();
        context.Activate(CreateProjectContext(workspace.Root));
        var catalog = new TestAssetCatalog(
            [
                new AssetRecord(new Uri("asset:///Content/Materials/Wood.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Materials/Wood.omat")),
            ]);
        var service = new MaterialPickerService(catalog, context);

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false });

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(AssetState.Stale, rows[0].State);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Wood.omat.json"), rows[0].MaterialUri);
    }

    [TestMethod]
    public async Task AssetsChangedMessage_WhenCatalogCacheHasNotSeenNewMaterial_ShouldPublishSourceRow()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Gold.omat.json"), 1.0f, 0.75f, 0.15f, 1.0f);
        var context = new ProjectContextService();
        context.Activate(CreateProjectContext(workspace.Root));
        var messenger = new StrongReferenceMessenger();
        var service = new MaterialPickerService(new TestAssetCatalog([]), context, messenger);

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));

        _ = messenger.Send(new AssetsChangedMessage(new Uri("asset:///Content/Materials/Gold.omat.json")));

        for (var i = 0; i < 20 && rows.Count == 0; i++)
        {
            await Task.Delay(25).ConfigureAwait(false);
        }

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Gold.omat.json"), rows[0].MaterialUri);
        Assert.AreEqual(AssetState.Source, rows[0].State);

        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false }).ConfigureAwait(false);

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Gold.omat.json"), rows[0].MaterialUri);
        Assert.AreEqual(AssetState.Source, rows[0].State);
    }

    private static ProjectContext CreateProjectContext(string projectRoot)
        => new()
        {
            ProjectId = Guid.NewGuid(),
            Name = "Test",
            Category = Category.Games,
            ProjectRoot = projectRoot,
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
            Scenes = [],
        };

    private static void WriteMaterial(string path, float r, float g, float b, float a)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = File.Create(path);
        MaterialSourceWriter.Write(
            stream,
            new MaterialSource(
                "oxygen.material.v1",
                "PBR",
                "Wood",
                new MaterialPbrMetallicRoughness(
                    r,
                    g,
                    b,
                    a,
                    metallicFactor: 0.0f,
                    roughnessFactor: 0.5f,
                    baseColorTexture: null,
                    metallicRoughnessTexture: null),
                normalTexture: null,
                occlusionTexture: null,
                alphaMode: MaterialAlphaMode.Opaque,
                alphaCutoff: 0.5f,
                doubleSided: false));
    }

    private sealed class TestAssetCatalog(IReadOnlyList<AssetRecord> records) : IAssetCatalog
    {
        public IObservable<AssetChange> Changes { get; } = new EmptyObservable<AssetChange>();

        public Task<IReadOnlyList<AssetRecord>> QueryAsync(
            AssetQuery query,
            CancellationToken cancellationToken = default)
        {
            _ = query;
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(records);
        }
    }

    private sealed class EmptyObservable<T> : IObservable<T>
    {
        public IDisposable Subscribe(IObserver<T> observer)
        {
            _ = observer;
            return new EmptyDisposable();
        }
    }

    private sealed class EmptyDisposable : IDisposable
    {
        public void Dispose()
        {
        }
    }

    private sealed class Observer<T>(Action<T> onNext) : IObserver<T>
    {
        public void OnCompleted()
        {
        }

        public void OnError(Exception error)
        {
            throw error;
        }

        public void OnNext(T value) => onNext(value);
    }

    private sealed class TempWorkspace : IDisposable
    {
        public TempWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-material-picker-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
        }

        public string Root { get; }

        public string SourcePath(string relative)
            => Path.Combine(this.Root, relative.Replace('/', Path.DirectorySeparatorChar));

        public void Dispose()
        {
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
