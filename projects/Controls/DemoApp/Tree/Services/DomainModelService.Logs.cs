// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Demo.Tree.Services;

/// <summary>
///    Logging partial methods for <see cref="DomainModelService" />.
/// </summary>
internal partial class DomainModelService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Unsupported parent type for entity insert: {ParentType}")]
    private static partial void LogUnsupportedEntityInsert(ILogger logger, string parentType);

    private void LogUnsupportedEntityInsert(object? parent)
        => LogUnsupportedEntityInsert(this.logger, parent?.ToString() ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Unsupported parent type for entity remove: {ParentType}")]
    private static partial void LogUnsupportedEntityRemove(ILogger logger, string parentType);

    private void LogUnsupportedEntityRemove(object? parent)
        => LogUnsupportedEntityRemove(this.logger, parent?.ToString() ?? "<null>");
}
