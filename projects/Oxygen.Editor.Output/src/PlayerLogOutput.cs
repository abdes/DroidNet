// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputConsole.Model;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.LivePreview.Play;
using Serilog.Events;

namespace Oxygen.Editor.Output;

/// <summary>
/// Adds the player's output to the output buffer, as the <see cref="OutputModule.PlayerSource"/> source; a new session's start is
/// marked in the output by an entry, and earlier output stays.
/// </summary>
internal sealed partial class PlayerLogOutput : IDisposable
{
    /// <summary>The message of the entry that marks a new play session's start.</summary>
    internal const string SessionStartedMessage = "Play session started";

    private readonly IPlayerLog log;
    private readonly OutputLogBuffer buffer;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="PlayerLogOutput"/> class.</summary>
    /// <param name="log">The player's output.</param>
    /// <param name="buffer">The buffer the Output panel shows.</param>
    public PlayerLogOutput(IPlayerLog log, OutputLogBuffer buffer)
    {
        ArgumentNullException.ThrowIfNull(log);
        ArgumentNullException.ThrowIfNull(buffer);
        this.log = log;
        this.buffer = buffer;
        this.log.LogReceived += this.OnLogReceived;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.log.LogReceived -= this.OnLogReceived;
    }

    private static LogEventLevel LevelOf(LogLevel level) => level switch
    {
        LogLevel.Critical => LogEventLevel.Fatal,
        LogLevel.Error => LogEventLevel.Error,
        LogLevel.Warning => LogEventLevel.Warning,
        LogLevel.Debug => LogEventLevel.Debug,
        LogLevel.Trace => LogEventLevel.Verbose,
        _ => LogEventLevel.Information,
    };

    private void OnLogReceived(object? sender, PlayerLogEventArgs args)
    {
        if (args.IsSessionStart)
        {
            this.buffer.Append(new OutputLogEntry
            {
                Timestamp = args.Timestamp,
                Level = LogEventLevel.Information,
                Source = OutputModule.PlayerSource,
                Message = SessionStartedMessage,
            });
            return;
        }

        this.buffer.Append(new OutputLogEntry
        {
            Timestamp = args.Timestamp,
            Level = LevelOf(args.Level),
            Source = OutputModule.PlayerSource,
            Channel = args.Channel,
            Message = args.Message,
        });
    }
}
