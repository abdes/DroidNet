// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Input.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector;
using Oxygen.Managed.Core;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal sealed partial class SceneAuthoringFixture
{
    public SceneNodeEditorViewModel CreateInspectorHost(string kind, bool realizeViews = false, Oxygen.Editor.Assets.IAssetFeed? assetProvider = null)
    {
        if (!string.Equals(kind, "Environment", StringComparison.Ordinal))
        {
            this.Select(this.Node);
        }

        var locator = new Mock<IViewLocator>();
        if (realizeViews)
        {
            _ = locator.Setup(value => value.ResolveView(It.IsAny<object>())).Returns((object model) => model is Oxygen.Editor.World.Inspector.Geometry.GeometryViewModel ? new Oxygen.Editor.World.Inspector.Geometry.GeometryView() : CreateNumericView((IPropertyEditor<SceneNode>)model));
        }

        var catalog = new Mock<Oxygen.Editor.Assets.IAssetFeed>();
        _ = catalog.SetupGet(value => value.Items).Returns(System.Reactive.Linq.Observable.Empty<IReadOnlyList<Oxygen.Editor.Assets.AssetEntry>>());
        _ = catalog.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
        return new(new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher) }, new ViewModelToView(locator.Object), Mock.Of<IAssetReveal>(), this.Area, assetProvider ?? catalog.Object, this.Preview.Object, this.Selection, this.Slots, this.Projects, new FakeInputSources(), interaction: this.interaction);
    }
}
