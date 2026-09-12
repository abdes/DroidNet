// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Releases WinUI's swap-chain references before native device teardown.</summary>
internal sealed partial class NativeEngineSession
{
    private void DetachSurfacePanel(Guid viewportId)
    {
        if (!this.surfacePanels.TryGetValue(viewportId, out var panel))
        {
            return;
        }

        var pointer = Marshal.GetIUnknownForObject(panel);
        try
        {
            this.Runner.DetachSwapChainPanel(pointer);
            _ = this.surfacePanels.Remove(viewportId);
        }
        finally
        {
            _ = Marshal.Release(pointer);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "All independent panel attachments are released; failed ownership is retained and reported for cleanup retry.")]
    private void DetachSurfacePanels()
    {
        List<Exception> failures = [];
        foreach (var viewportId in this.surfacePanels.Keys.ToArray())
        {
            try
            {
                this.DetachSurfacePanel(viewportId);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        if (failures.Count != 0)
        {
            throw new AggregateException("Some viewport panels could not release their swap chains.", failures);
        }
    }
}
