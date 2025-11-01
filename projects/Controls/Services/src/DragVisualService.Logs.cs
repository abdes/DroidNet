// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Implementation of <see cref="IDragVisualService"/> using a Win32 layered window for the drag
///     overlay. The overlay is topmost, non-activating, click-through, and survives source
///     AppWindow closure.
/// </summary>
public partial class DragVisualService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{ServiceName} created.")]
    private static partial void LogCreated(ILogger logger, string serviceName);

    private void LogCreated() => LogCreated(this.logger, nameof(DragVisualService));
}
