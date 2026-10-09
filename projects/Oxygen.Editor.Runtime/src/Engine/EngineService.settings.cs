// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Engine-wide presentation settings applied to the running native session.</summary>
public sealed partial class EngineService
{
    /// <inheritdoc/>
    public void SetVSyncEnabled(bool enabled)
    {
        this.EnsureIsReadyOrRunning().SetVSyncEnabled(enabled);
        this.LogVSyncSet(enabled);
    }

    /// <inheritdoc/>
    public void SetAlwaysRenderPanes(bool alwaysRender)
    {
        this.EnsureIsReadyOrRunning().SetAlwaysRenderPanes(alwaysRender);
        this.LogAlwaysRenderPanesSet(alwaysRender);
    }
}
