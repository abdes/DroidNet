// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Logging helpers for <see cref="CookPublicationService"/>.</summary>
public sealed partial class CookPublicationService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cooked {Owner} root '{RootName}' at '{RootPath}' changed after publication {PublicationId} of project '{ProjectRoot}': "
            + "recorded source {RecordedSourceKey} index {RecordedIndexSha256}, found source {ActualSourceKey} index {ActualIndexSha256}. "
            + "Republishing the project from its current libraries.")]
    private static partial void LogCookedRootChangedCore(
        ILogger logger,
        CookPublicationRootOwner? owner,
        string? rootName,
        string? rootPath,
        Guid? publicationId,
        string projectRoot,
        Guid? recordedSourceKey,
        string? recordedIndexSha256,
        Guid? actualSourceKey,
        string? actualIndexSha256);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cooked content '{RootName}' at '{RootPath}' cannot be read until it is recooked: it was cooked in another index format or its generation is gone.")]
    private static partial void LogRootNeedsRecooking(ILogger logger, string rootName, string rootPath);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cooked library '{LibraryName}' at '{LibraryPath}' is left out of the cooked content: {Reason}.")]
    private static partial void LogLibraryUnreadable(ILogger logger, string libraryName, string libraryPath, string reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Republished cooked content of project '{ProjectRoot}' as publication {PublicationId}.")]
    private static partial void LogRepublished(ILogger logger, Guid? publicationId, string projectRoot);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not republish cooked content of project '{ProjectRoot}'; its cooked assets stay unavailable.")]
    private static partial void LogRepublishFailed(ILogger logger, Exception exception, string projectRoot);

    private static void LogCookedRootChanged(ILogger logger, CookedLibraryChangedException changed, Guid? publicationId, string projectRoot)
    {
        if (!logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        LogCookedRootChangedCore(
            logger,
            changed.Root?.Owner,
            changed.Root?.Name,
            changed.RootPath,
            publicationId,
            projectRoot,
            changed.Root?.SourceKey,
            changed.Root?.IndexSha256,
            changed.ActualSourceKey,
            changed.ActualIndexSha256);
    }
}
