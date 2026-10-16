// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.MaterialEditor;

/// <content>Logging for <see cref="MaterialEdits"/>.</content>
public sealed partial class MaterialEdits
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "Rejected an edit of the material '{Material}' ({Code}): {Message}")]
    private partial void LogEditRejected(string material, string code, string message);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Applied {PropertyCount} properties to the material '{Material}'.")]
    private partial void LogPropertiesApplied(string material, int propertyCount);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Undo or redo of the material '{Material}' restored references to deleted textures: {Textures}.")]
    private partial void LogRestoredDeletedTexture(string material, string textures);
}
