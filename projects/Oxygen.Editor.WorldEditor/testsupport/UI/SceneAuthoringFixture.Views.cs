// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.Mvvm;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Catalog;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal sealed partial class SceneAuthoringFixture
{
    public SceneNodeEditorViewModel CreateInspectorHost(string kind, bool realizeViews = false, Oxygen.Editor.WorldEditor.Documents.Commands.ISceneDocumentCommandService? commandService = null, ISceneContentDemandService? contentDemand = null, Oxygen.Editor.ContentBrowser.AssetIdentity.IContentBrowserAssetProvider? assetProvider = null)
    {
        IList<SceneNode> selection = string.Equals(kind, "Environment", StringComparison.Ordinal) ? [] : [this.Node];
        this.Messenger.Register<SceneNodeSelectionRequestMessage>(this, (_, message) => message.Reply(selection));
        _ = this.Documents.Setup(service => service.GetOpenDocuments(It.IsAny<WindowId>())).Returns([this.Context.Metadata]);
        _ = this.Documents.Setup(service => service.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(this.Scene.Id);
        _ = this.Sync.Setup(service => service.GetDocumentScene(this.Context.Metadata)).Returns(this.Scene);
        var locator = new Mock<IViewLocator>();
        if (realizeViews)
        {
            _ = locator.Setup(value => value.ResolveView(It.IsAny<object>())).Returns((object model) => model is Oxygen.Editor.World.Inspector.Geometry.GeometryViewModel ? new Oxygen.Editor.World.Inspector.Geometry.GeometryView() : CreateNumericView((IPropertyEditor<SceneNode>)model));
        }

        var catalog = new Mock<Oxygen.Editor.ContentBrowser.AssetIdentity.IContentBrowserAssetProvider>();
        _ = catalog.SetupGet(value => value.Items).Returns(System.Reactive.Linq.Observable.Empty<IReadOnlyList<Oxygen.Editor.ContentBrowser.AssetIdentity.ContentBrowserAssetItem>>());
        _ = catalog.Setup(value => value.RefreshAsync(It.IsAny<Oxygen.Editor.ContentBrowser.AssetIdentity.AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var materials = new Mock<IMaterialPickerService>();
        _ = materials.SetupGet(value => value.Results).Returns(System.Reactive.Linq.Observable.Return<IReadOnlyList<MaterialPickerResult>>([]));
        _ = materials.Setup(value => value.RefreshAsync(It.IsAny<MaterialPickerFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
        return new(new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher) }, new ViewModelToView(locator.Object), this.Messenger, commandService ?? this.Commands, this.Documents.Object, default, assetProvider ?? catalog.Object, materials.Object, this.Sync.Object, this.Selection, new Oxygen.Testing.BuiltinCatalogDiscoveryFixture(), contentDemand ?? Mock.Of<ISceneContentDemandService>(), this.Slots.Object, this.Projects);
    }

    public void ConfigureObservedSync(SceneEngineSync throttle, List<DateTimeOffset> previews, Action terminal)
    {
        _ = this.Sync.Setup(value => value.TryPreviewSyncAsync(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<DateTimeOffset>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>())).Returns((Guid sceneId, Guid nodeId, DateTimeOffset now, Func<CancellationToken, Task<SyncOutcome>> action, CancellationToken token) => throttle.TryPreviewSyncAsync(sceneId, nodeId, now, ct =>
        {
            previews.Add(now);
            return action(ct);
        }, token));
        _ = this.Sync.Setup(value => value.CompleteTerminalSyncAsync(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>())).Returns((Guid sceneId, Guid nodeId, Func<CancellationToken, Task<SyncOutcome>> action, CancellationToken token) => throttle.CompleteTerminalSyncAsync(sceneId, nodeId, ct =>
        {
            terminal();
            return action(ct);
        }, token));
    }
}
