// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Discovery;

/// <summary>Provides current or last-known built-in metadata for authoring discovery.</summary>
public interface IBuiltinCatalogDiscovery
{
    /// <summary>Occurs after the discovery snapshot changes.</summary>
    public event EventHandler? Changed;

    /// <summary>Gets the latest immutable discovery snapshot.</summary>
    public BuiltinCatalogSnapshot Snapshot { get; }

    /// <summary>Initializes discovery once, sharing work across readers.</summary>
    /// <param name="cancellationToken">Cancels this reader's wait.</param>
    /// <returns>The current discovery snapshot.</returns>
    public Task<BuiltinCatalogSnapshot> GetAsync(CancellationToken cancellationToken = default);

    /// <summary>Checks the SDK again and refreshes its metadata when the native producer changes.</summary>
    /// <param name="cancellationToken">Cancels this reader's wait.</param>
    /// <returns>The refreshed snapshot, retaining usable cached metadata if the SDK is unavailable.</returns>
    public Task<BuiltinCatalogSnapshot> RefreshAsync(CancellationToken cancellationToken = default);
}
