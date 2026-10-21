// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Materials.Documents;

/// <content>Logging helpers for <see cref="MaterialEdits"/>.</content>
public sealed partial class MaterialEdits
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Rejected an edit of the material '{Material}' ({Code}): {Message}")]
    private static partial void LogEditRejected(ILogger logger, string material, string code, string message);

    private void LogEditRejected(string material, string code, string message)
        => LogEditRejected(this.logger, material, code, message);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Applied {PropertyCount} properties to the material '{Material}'.")]
    private static partial void LogPropertiesApplied(ILogger logger, string material, int propertyCount);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogPropertiesApplied(string material, int propertyCount)
        => LogPropertiesApplied(this.logger, material, propertyCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Undo or redo of the material '{Material}' restored references to deleted textures: {Textures}.")]
    private static partial void LogRestoredDeletedTexture(ILogger logger, string material, string textures);

    private void LogRestoredDeletedTexture(string material, string textures)
        => LogRestoredDeletedTexture(this.logger, material, textures);
}
