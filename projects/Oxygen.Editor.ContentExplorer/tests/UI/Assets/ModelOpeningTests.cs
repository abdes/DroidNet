// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using System.Text.Json;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Models;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerControls;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerTestData;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Assets;

/// <summary>Opening an imported model opens its Model Editor (model editor 4); reimporting is a command of its own.</summary>
[TestClass]
internal sealed class ModelOpeningTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Car.glb");

    /// <summary>Double-clicking an imported model opens its document and reimports nothing.</summary>
    /// <returns>The asynchronous browser invocation test.</returns>
    [TestMethod]
    public Task ImportedModelOpensItsDocument() => EnqueueAsync(async () =>
    {
        using var session = CreateQuerySession();
        var source = session.Context.ProjectRoot.Combine("Content/Car.glb");
        var files = session.Context.FileSystem;
        _ = files.Directory.CreateDirectory(files.Path.GetDirectoryName(source.FullPath)!);
        files.File.WriteAllBytes(source.FullPath, "glTF"u8.ToArray());
        var record = ModelImportSettings.PathFor(session.Context.ProjectRoot.FullPath, Guid.CreateVersion7());
        _ = files.Directory.CreateDirectory(files.Path.GetDirectoryName(record)!);
        using (var settings = JsonDocument.Parse("{}"))
        {
            files.File.WriteAllBytes(record, ModelImportSettings.Create(record, source.FullPath, Car.VirtualPath, settings.RootElement).ToBytes());
        }

        var item = new AssetEntry(Car, Car.Name, AssetKind.Model, AssetState.Source, DerivedState: null, AssetRuntimeAvailability.NotApplicable, Car.VirtualPath, source, DescriptorPath: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true)
        {
            ImportSource = Car,
        };
        using var items = new BehaviorSubject<IReadOnlyList<AssetEntry>>([item]);
        var provider = CreateQueryProvider(items);
        var state = new ContentExplorerState();
        using var layout = new ListLayoutViewModel(provider.Object, session, state, CreateStatusHosting());
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await layout.InitialLoad.ConfigureAwait(true);
        var layoutView = new ListLayoutView
        {
            ViewModel = layout,
        };
        var placement = new Mock<IPlacementService>();
        var pipeline = new Mock<IContentPipelineService>();
        using var browser = CreateAssetsModel(provider.Object, session, state, layout, layoutView, pipeline.Object, placement: placement.Object);
        await browser.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        browser.LoadContent(layout, "right");
        await LoadTestContentAsync(layoutView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        layout.InvokeItemCommand.Execute(item);
        await WaitForRenderAsync().ConfigureAwait(true);

        placement.Verify(value => value.OpenDocumentAsync(Car, It.IsAny<WindowPlacement>(), It.IsAny<CancellationToken>()), Times.Once);
        pipeline.Verify(value => value.ReimportSourceAsync(It.IsAny<AssetPath>(), It.IsAny<Oxygen.Editor.Projects.ProjectContext>(), It.IsAny<CancellationToken>()), Times.Never);
    });
}
