// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
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
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "ENGINE CONTENT PAK UNAVAILABLE: '{Path}' could not be read, so no /Engine asset (basic shapes, the default material) is listed and references to them are missing. Install the SDK, or rebuild the engine's oxygen-engine-content target and reinstall it.")]
    private static partial void LogEngineContentUnavailable(ILogger logger, FileSystemPath path, Exception exception);

    private void LogEngineContentUnavailable(FileSystemPath path, Exception exception) => LogEngineContentUnavailable(this.logger, path, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Pak file mount '{Mount}' ('{Path}') could not be read, so none of its assets is listed and references to them are missing.")]
    private static partial void LogPakUnavailable(ILogger logger, string mount, FileSystemPath path, Exception exception);

    private void LogPakUnavailable(string mount, FileSystemPath path, Exception exception) => LogPakUnavailable(this.logger, mount, path, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "An observer of the asset catalog failed after the catalog was updated; the update stays in place.")]
    private static partial void LogObserverFailed(ILogger logger, Exception exception);

    private void LogObserverFailed(Exception exception) => LogObserverFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "{Kind} change of '{Asset}' arrived from a catalog that is not installed yet; it is held, and reported only if that catalog is installed.")]
    private static partial void LogChangeHeld(ILogger logger, AssetChangeKind kind, AssetPath asset);

    [Conditional("DEBUG")]
    private void LogChangeHeld(AssetChangeKind kind, AssetPath asset) => LogChangeHeld(this.logger, kind, asset);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Reported {Count} changes that were held while the asset catalogs were being installed.")]
    private static partial void LogHeldChangesReported(ILogger logger, int count);

    private void LogHeldChangesReported(int count) => LogHeldChangesReported(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "{Kind} change of '{Asset}' came from a catalog that is no longer installed; it is not reported.")]
    private static partial void LogChangeIgnored(ILogger logger, AssetChangeKind kind, AssetPath asset);

    private void LogChangeIgnored(AssetChangeKind kind, AssetPath asset) => LogChangeIgnored(this.logger, kind, asset);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Folder change of '{Folder}' came from a catalog that is not installed; it is not reported.")]
    private static partial void LogFolderChangeDropped(ILogger logger, AssetPath folder);

    private void LogFolderChangeDropped(AssetPath folder) => LogFolderChangeDropped(this.logger, folder);
}
