// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets;

/// <inheritdoc cref="AssetCatalog"/>
/// <remarks>
/// Levels: Debug for each change held back while catalogs are installed; Information for the held changes reported
/// once they are; Warning for a change that is not reported at all, and for an observer that failed.
/// </remarks>
public sealed partial class AssetCatalog
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "ENGINE CONTENT PAK UNAVAILABLE: '{Path}' could not be read, so no /Engine asset (basic shapes, the default material) is listed and references to them are missing. Install the SDK, or rebuild the engine's oxygen-engine-content target and reinstall it.")]
    private partial void LogEngineContentUnavailable(FileSystemPath path, Exception exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Pak file mount '{Mount}' ('{Path}') could not be read, so none of its assets is listed and references to them are missing.")]
    private partial void LogPakUnavailable(string mount, FileSystemPath path, Exception exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "An observer of the asset catalog failed after the catalog was updated; the update stays in place.")]
    private partial void LogObserverFailed(Exception exception);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "{Kind} change of '{Asset}' arrived from a catalog that is not installed yet; it is held, and reported only if that catalog is installed.")]
    private static partial void LogChangeHeld(ILogger logger, AssetChangeKind kind, AssetPath asset);

    [LoggerMessage(Level = LogLevel.Information, Message = "Reported {Count} changes that were held while the asset catalogs were being installed.")]
    private partial void LogHeldChangesReported(int count);

    [LoggerMessage(Level = LogLevel.Warning, Message = "{Kind} change of '{Asset}' came from a catalog that is no longer installed; it is not reported.")]
    private partial void LogChangeIgnored(AssetChangeKind kind, AssetPath asset);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Folder change of '{Folder}' came from a catalog that is not installed; it is not reported.")]
    private partial void LogFolderChangeDropped(AssetPath folder);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogChangeHeld(AssetChangeKind kind, AssetPath asset)
#pragma warning disable CA1873 // Compiled out of release builds by Conditional("DEBUG")
        => LogChangeHeld(this.logger, kind, asset);
#pragma warning restore CA1873
}
