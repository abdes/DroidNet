// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Catalog;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Model;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentBrowser.Messages;
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
        var service = new MaterialPickerService(new TestAssetProvider(
            [
                CreateItem(
                    new Uri("asset:///Content/Materials/Wood.omat.json"),
                    AssetState.Descriptor,
                    descriptorPath: workspace.SourcePath("Content/Materials/Wood.omat.json")),
            ]));

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false });

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(AssetState.Descriptor, rows[0].PrimaryState);
        Assert.IsNull(rows[0].DerivedState);
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

        var service = new MaterialPickerService(new TestAssetProvider(
            [
                CreateItem(
                    new Uri("asset:///Content/Materials/Wood.omat.json"),
                    AssetState.Descriptor,
                    derivedState: AssetState.Stale,
                    descriptorPath: sourcePath,
                    cookedPath: cookedPath),
            ]));

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false });

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(AssetState.Descriptor, rows[0].PrimaryState);
        Assert.AreEqual(AssetState.Stale, rows[0].DerivedState);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Wood.omat.json"), rows[0].MaterialUri);
    }

    [TestMethod]
    public async Task RefreshAsync_WhenCookedFilterIsOff_ShouldKeepDescriptorRowsWithCookedDerivedState()
    {
        var service = new MaterialPickerService(new TestAssetProvider(
            [
                CreateItem(
                    new Uri("asset:///Content/Materials/Red.omat.json"),
                    AssetState.Descriptor,
                    derivedState: AssetState.Cooked),
            ]));

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false, IncludeCooked = false });

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(AssetState.Descriptor, rows[0].PrimaryState);
        Assert.AreEqual(AssetState.Cooked, rows[0].DerivedState);
    }

    [TestMethod]
    public async Task RefreshAsync_WhenCookedFilterIsOff_ShouldKeepDescriptorRowsWithStaleDerivedState()
    {
        var service = new MaterialPickerService(new TestAssetProvider(
            [
                CreateItem(
                    new Uri("asset:///Content/Materials/Red.omat.json"),
                    AssetState.Descriptor,
                    derivedState: AssetState.Stale),
            ]));

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false, IncludeCooked = false });

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(AssetState.Descriptor, rows[0].PrimaryState);
        Assert.AreEqual(AssetState.Stale, rows[0].DerivedState);
    }

    [TestMethod]
    public async Task RefreshAsync_WhenSourceFilterIsOff_ShouldKeepCookedOnlyRows()
    {
        var service = new MaterialPickerService(new TestAssetProvider(
            [
                CreateItem(
                    new Uri("asset:///Content/Materials/Red.omat"),
                    AssetState.Cooked),
            ]));

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false, IncludeSource = false });

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(AssetState.Cooked, rows[0].PrimaryState);
    }

    [TestMethod]
    public async Task ResolveAsync_WhenCurrentAssignmentIsMissing_ShouldPinMissingRowEvenWhenMissingFilterIsOff()
    {
        var service = new MaterialPickerService(new TestAssetProvider([]));

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false, IncludeMissing = false });
        _ = await service.ResolveAsync(new Uri("asset:///Content/Materials/Missing.omat.json"));

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Missing.omat.json"), rows[0].MaterialUri);
        Assert.AreEqual(AssetState.Missing, rows[0].PrimaryState);
    }

    [TestMethod]
    public async Task ResolveAsync_WhenCurrentAssignmentIsBroken_ShouldPinBrokenRowEvenWhenMissingFilterIsOff()
    {
        using var workspace = new TempWorkspace();
        var descriptorPath = workspace.SourcePath("Content/Materials/Broken.omat.json");
        Directory.CreateDirectory(Path.GetDirectoryName(descriptorPath)!);
        await File.WriteAllTextAsync(descriptorPath, "{ invalid json");
        var provider = new TestAssetProvider(
            [
                CreateItem(
                    new Uri("asset:///Content/Materials/Broken.omat.json"),
                    AssetState.Broken,
                    descriptorPath: descriptorPath),
            ]);
        var service = new MaterialPickerService(provider);

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false, IncludeMissing = false });
        _ = await service.ResolveAsync(new Uri("asset:///Content/Materials/Broken.omat.json"));

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(AssetState.Broken, rows[0].PrimaryState);
        Assert.AreEqual(descriptorPath, rows[0].DescriptorPath);
    }

    [TestMethod]
    public async Task ResolveAsync_WhenUriIsNotMaterialDescriptor_ShouldNotPinMissingRow()
    {
        var service = new MaterialPickerService(new TestAssetProvider([]));

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false, IncludeMissing = false });
        var result = await service.ResolveAsync(new Uri("asset:///Content/Materials/Notes.txt"));

        Assert.IsNull(result);
        Assert.AreEqual(0, rows.Count);
    }

    [TestMethod]
    public async Task RefreshAsync_WhenBrokenDescriptorIsHidden_ShouldNotThrowWhileReadingPreview()
    {
        using var workspace = new TempWorkspace();
        var descriptorPath = workspace.SourcePath("Content/Materials/Broken.omat.json");
        Directory.CreateDirectory(Path.GetDirectoryName(descriptorPath)!);
        await File.WriteAllTextAsync(descriptorPath, "{ invalid json");
        var service = new MaterialPickerService(new TestAssetProvider(
            [
                CreateItem(
                    new Uri("asset:///Content/Materials/Broken.omat.json"),
                    AssetState.Broken,
                    descriptorPath: descriptorPath),
            ]));

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false, IncludeMissing = false });

        Assert.AreEqual(0, rows.Count);
    }

    [TestMethod]
    public async Task RefreshAsync_ShouldApplySearchTextLocally()
    {
        var service = new MaterialPickerService(new TestAssetProvider(
            [
                CreateItem(new Uri("asset:///Content/Materials/Red.omat.json"), AssetState.Descriptor),
                CreateItem(new Uri("asset:///Content/Materials/Blue.omat.json"), AssetState.Descriptor),
            ]));

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false, SearchText = "Blue" });

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Blue.omat.json"), rows[0].MaterialUri);
    }

    [TestMethod]
    public async Task RefreshAsync_ShouldOnlyProjectMaterialRows()
    {
        var service = new MaterialPickerService(new TestAssetProvider(
            [
                CreateItem(new Uri("asset:///Content/Materials/Red.omat.json"), AssetState.Descriptor),
                CreateItem(new Uri("asset:///Content/Materials/Notes.txt"), AssetState.Source, kind: AssetKind.Unknown),
                CreateItem(new Uri("asset:///Content/Images/Preview.png"), AssetState.Source, kind: AssetKind.Image),
            ]));

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));
        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false });

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Red.omat.json"), rows[0].MaterialUri);
    }

    [TestMethod]
    public async Task AssetsChangedMessage_WhenCatalogCacheHasNotSeenNewMaterial_ShouldPublishSourceRow()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Gold.omat.json"), 1.0f, 0.75f, 0.15f, 1.0f);
        var messenger = new StrongReferenceMessenger();
        var provider = new TestAssetProvider(
            [
                CreateItem(
                    new Uri("asset:///Content/Materials/Gold.omat.json"),
                    AssetState.Descriptor,
                    descriptorPath: workspace.SourcePath("Content/Materials/Gold.omat.json")),
            ]);
        var service = new MaterialPickerService(provider, messenger);

        IReadOnlyList<MaterialPickerResult> rows = [];
        using var subscription = service.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => rows = value));

        _ = messenger.Send(new AssetsChangedMessage(new Uri("asset:///Content/Materials/Gold.omat.json")));

        for (var i = 0; i < 20 && rows.Count == 0; i++)
        {
            await Task.Delay(25).ConfigureAwait(false);
        }

        var refreshedRow = rows.Single(row => row.MaterialUri == new Uri("asset:///Content/Materials/Gold.omat.json"));
        Assert.AreEqual(AssetState.Descriptor, refreshedRow.PrimaryState);

        await service.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false }).ConfigureAwait(false);

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Gold.omat.json"), rows[0].MaterialUri);
        Assert.AreEqual(AssetState.Descriptor, rows[0].PrimaryState);
    }

    private static ContentBrowserAssetItem CreateItem(
        Uri uri,
        AssetState primaryState,
        AssetState? derivedState = null,
        string? descriptorPath = null,
        string? cookedPath = null,
        AssetKind kind = AssetKind.Material)
        => new(
            IdentityUri: uri,
            DisplayName: Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(uri.AbsolutePath)),
            Kind: kind,
            PrimaryState: primaryState,
            DerivedState: derivedState,
            RuntimeAvailability: AssetRuntimeAvailability.NotMounted,
            DisplayPath: AssetUriHelper.GetVirtualPath(uri),
            SourcePath: null,
            DescriptorPath: descriptorPath,
            CookedUri: null,
            CookedPath: cookedPath,
            AssetGuid: null,
            DiagnosticCodes: [],
            IsSelectable: true);

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

    private sealed class TestAssetProvider(IReadOnlyList<ContentBrowserAssetItem> rows) : IContentBrowserAssetProvider
    {
        private readonly ReplayObservable<IReadOnlyList<ContentBrowserAssetItem>> items = new(rows);

        public IObservable<IReadOnlyList<ContentBrowserAssetItem>> Items => this.items;

        public Task RefreshAsync(AssetBrowserFilter filter, CancellationToken cancellationToken = default)
        {
            _ = filter;
            cancellationToken.ThrowIfCancellationRequested();
            this.items.Publish(rows);
            return Task.CompletedTask;
        }

        public Task<ContentBrowserAssetItem?> ResolveAsync(Uri uri, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(rows.FirstOrDefault(row => string.Equals(row.IdentityUri.ToString(), uri.ToString(), StringComparison.OrdinalIgnoreCase)));
        }
    }

    private sealed class ReplayObservable<T>(T value) : IObservable<T>
    {
        private readonly List<IObserver<T>> observers = [];
        private T value = value;

        public IDisposable Subscribe(IObserver<T> observer)
        {
            this.observers.Add(observer);
            observer.OnNext(this.value);
            return new Subscription<T>(this.observers, observer);
        }

        public void Publish(T nextValue)
        {
            this.value = nextValue;
            foreach (var observer in this.observers.ToArray())
            {
                observer.OnNext(this.value);
            }
        }
    }

    private sealed class Subscription<T>(List<IObserver<T>> observers, IObserver<T> observer) : IDisposable
    {
        public void Dispose() => observers.Remove(observer);
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
