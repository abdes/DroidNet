// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Workspace;

/// <summary>Holds the workspace's preview content demand for the lifetime of its container, which disposes it.</summary>
/// <param name="demand">The started demand.</param>
internal sealed class PreviewContentDemand(IDisposable demand) : IDisposable
{
    /// <inheritdoc/>
    public void Dispose() => demand.Dispose();
}
