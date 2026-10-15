// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Moq;
using Oxygen.Editor.Assets.Runtime;

namespace Oxygen.Editor.Assets.TestSupport;

/// <summary>Supplies the preview status of an editor whose preview is not running.</summary>
internal static class AssetStatusFixture
{
    /// <summary>Creates a preview status source for tests concerned with cooked-file facts.</summary>
    /// <returns>A preview that is not running, with no mounts or asset requests.</returns>
    public static IAssetRuntimeStatusSource CreateUnavailableRuntimeStatus()
    {
        var status = new Mock<IAssetRuntimeStatusSource>();
        _ = status.Setup(value => value.GetStatus(It.IsAny<Guid?>())).Returns(new AssetRuntimeStatus(AssetRuntimeContentState.Unavailable, [], [], "The preview is not running."));
        return status.Object;
    }
}
