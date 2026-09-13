// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Discovery;

/// <summary>Validates and atomically retains complete native catalog documents.</summary>
public sealed partial class BuiltinCatalogDiscovery
{
    private static bool IsCatalogFailure(Exception exception)
        => exception is IOException or InvalidDataException or UnauthorizedAccessException or JsonException or InvalidOperationException
            or ArgumentException or KeyNotFoundException or ContentPipelineTerminationException;

    private static void Validate(BuiltinGeometryCatalog catalog)
    {
        if (!string.Equals(catalog.MountName, AssetUris.ContentMountPoint, StringComparison.Ordinal)
            || catalog.Geometries.Any(static definition => !string.Equals(definition.AssetUri.Scheme, AssetUris.Scheme, StringComparison.Ordinal)
                || !definition.AssetUri.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.Ordinal)))
        {
            throw new InvalidDataException("The built-in catalog must contain engine-generated identities in the Content output mount.");
        }

        // Exercise the same projections used by discovery and resolution before retaining metadata.
        _ = catalog.CreateCatalogRecords();
        _ = catalog.CreateAssets();
    }

    private async Task<BuiltinGeometryCatalog?> ReadCacheAsync(CancellationToken cancellationToken)
    {
        try
        {
            var saved = await this.files.ReadAsync(this.cachePath, cancellationToken).ConfigureAwait(false);
            if (!saved.Version.Exists)
            {
                return null;
            }

            var catalog = BuiltinGeometryCatalog.Parse(Encoding.UTF8.GetString(saved.Content.AsSpan()));
            Validate(catalog);
            return catalog;
        }
        catch (Exception exception) when (IsCatalogFailure(exception))
        {
            this.LogCacheUnavailable(exception);
            return null;
        }
    }

    private async Task WriteCacheAsync(BuiltinGeometryCatalog catalog, CancellationToken cancellationToken)
    {
        try
        {
            Directory.CreateDirectory(this.cacheRoot);
            var saved = await this.files.ReadAsync(this.cachePath, cancellationToken).ConfigureAwait(false);
            _ = await this.files.WriteAsync(this.cachePath, Encoding.UTF8.GetBytes(catalog.ToJson()), saved.Version, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            this.LogCacheUnavailable(exception);
        }
    }

    private async Task<BuiltinGeometryCatalog> QueryNativeAsync(NativeArtifactLease artifacts, CancellationToken cancellationToken)
    {
        try
        {
            return await this.nativeCatalog.GetBuiltinGeometryCatalogAsync(this.queryRoot, AssetUris.ContentMountPoint, cancellationToken, artifacts).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException exception)
        {
            // The provider borrowed this lease; retain it until the worker and readers actually stop.
            await exception.DrainCompletion.ConfigureAwait(false);
            throw;
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A discovery presentation observer cannot invalidate the captured catalog or prevent other observers updating.")]
    private void PublishChanged()
    {
        foreach (var handler in this.Changed?.GetInvocationList() ?? [])
        {
            try
            {
                ((EventHandler)handler)(this, EventArgs.Empty);
            }
            catch (Exception exception)
            {
                this.LogObserverFailed(exception);
            }
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Disposal observes the already-owned background task and releases cancellation only after its native worker drains.")]
    private async Task DisposeAfterRefreshAsync(Task<BuiltinCatalogSnapshot> pending)
    {
        try
        {
            _ = await pending.ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            this.LogRetirement(exception);
        }
        finally
        {
            this.lifetime.Dispose();
        }
    }

    [LoggerMessage(Level = LogLevel.Warning, Message = "The SDK could not provide its built-in catalog.")]
    private partial void LogSdkUnavailable(Exception exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The last-known built-in catalog could not be read or updated.")]
    private partial void LogCacheUnavailable(Exception exception);

    [LoggerMessage(Level = LogLevel.Error, Message = "A built-in catalog observer failed.")]
    private partial void LogObserverFailed(Exception exception);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Built-in catalog discovery ended while its workspace was closing.")]
    private partial void LogRetirement(Exception exception);
}
