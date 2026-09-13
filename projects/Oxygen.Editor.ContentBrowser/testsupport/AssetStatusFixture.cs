// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Testing;

/// <summary>Supplies an idle shared status feed to tests concerned only with document editing.</summary>
internal static class AssetStatusFixture
{
    /// <summary>Gets a read-only provider with no catalog entries or background work.</summary>
    public static IContentBrowserAssetProvider EmptyProvider { get; } = CreateEmptyProvider();

    /// <summary>Creates an unavailable native boundary for tests concerned with cooked-file facts.</summary>
    /// <returns>A runtime with no mounts or asset requests.</returns>
    public static IEngineService CreateUnavailableRuntime()
    {
        var commands = new Mock<IRuntimeWorldCommands>();
        _ = commands.SetupGet(value => value.AssetRequests).Returns([]);
        var runtime = new Mock<IEngineService>();
        _ = runtime.SetupGet(value => value.WorldCommands).Returns(commands.Object);
        _ = runtime.SetupGet(value => value.ContentStatus).Returns(new RuntimeContentSnapshot(Guid.Empty, 0, RuntimeContentState.Unavailable, [], "The preview is not running."));
        return runtime.Object;
    }

    private static IContentBrowserAssetProvider CreateEmptyProvider()
    {
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(Observable.Empty<IReadOnlyList<ContentBrowserAssetItem>>());
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        return provider.Object;
    }
}
