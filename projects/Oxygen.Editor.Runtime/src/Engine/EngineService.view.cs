// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// EngineService: view-management partial implementation.
/// Keeps view lifecycle operations separated from the core engine startup/lease logic.
/// </summary>
public sealed partial class EngineService
{
    /// <inheritdoc/>
    public async Task<ViewIdManaged> CreateViewAsync(ViewConfigManaged config, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(config);
        this.ThrowIfDisposed();

        // The engine context must have been created.
        this.EnsureEngineCreated();
        Debug.Assert(this.engineRunner != null, "Engine runner should be initialized when engine exists.");

        try
        {
            // Forward to the native interop runner. The runner method returns
            // Task<ViewIdManaged> which maps directly into C# Task<ViewIdManaged>.
            var result = await this.engineRunner.CreateViewAsync(this.engineContext, config).ConfigureAwait(true);
            return result;
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "CreateViewAsync failed for view '{Name}'", config?.Name);
            throw;
        }
    }

    /// <inheritdoc/>
    public async Task<bool> DestroyViewAsync(ViewIdManaged viewId, CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        this.EnsureEngineCreated();
        Debug.Assert(this.engineRunner != null, "Engine runner should be initialized when engine exists.");

        try
        {
            var ok = await this.engineRunner.DestroyViewAsync(this.engineContext, viewId).ConfigureAwait(true);
            return ok;
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "DestroyViewAsync failed for view {ViewId}", viewId);
            return false;
        }
    }
}
