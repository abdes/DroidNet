// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using FluentAssertions;
using Oxygen.Interop.Logging;

namespace DroidNet.Oxygen.Editor.Interop.Tests;

[TestClass]
public class LogHandlerTests
{
    [TestMethod]
    public void InstallCustomLogHandler()
    {
        var messages = new Dictionary<string, Loguru.Verbosity>(StringComparer.Ordinal)
        {
            { "CUSTOM_HANDLER_INFO: Some information...", Loguru.Verbosity.Verbosity_INFO },
            { "CUSTOM_HANDLER_ERROR: Be careful!", Loguru.Verbosity.Verbosity_ERROR },
            { "CUSTOM_HANDLER_WARNING: BOOOOOM!!!!!", Loguru.Verbosity.Verbosity_WARNING },
        };

        using var loguru = new Loguru();
        loguru.AddLogHandlerCallback(
            (message) =>
            {
                if (!message.Message.StartsWith("CUSTOM_HANDLER_", StringComparison.Ordinal))
                {
                    return;
                }

                var verbosity = message.Message switch
                {
                    var m when m.StartsWith("CUSTOM_HANDLER_INFO", StringComparison.Ordinal) => Loguru.Verbosity.Verbosity_INFO,
                    var m when m.StartsWith("CUSTOM_HANDLER_WARNING", StringComparison.Ordinal) => Loguru.Verbosity.Verbosity_WARNING,
                    var m when m.StartsWith("CUSTOM_HANDLER_ERROR", StringComparison.Ordinal) => Loguru.Verbosity.Verbosity_ERROR,
                    _ => Loguru.Verbosity.Verbosity_FATAL,
                };

                _ = verbosity.Should().Be(messages[message.Message]);
            },
            Loguru.Verbosity.Verbosity_INFO);

        foreach (var item in messages)
        {
            loguru.LogMessage(item.Value, item.Key);
        }

        // Uninstall the log handler before the loguru wrapper gets disposed
        loguru.RemoveLogHandlerCallback();
    }
}
