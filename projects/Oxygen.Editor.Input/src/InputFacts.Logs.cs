// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>
/// Logging helpers for <see cref="InputFacts"/>.
/// </summary>
public sealed partial class InputFacts
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not read the input sources of `{ProjectRoot}`")]
    private static partial void LogProjectScanFailed(ILogger logger, FileSystemPath projectRoot, Exception exception);

    private void LogProjectScanFailed(FileSystemPath projectRoot, Exception exception) => LogProjectScanFailed(this.logger, projectRoot, exception);
}
