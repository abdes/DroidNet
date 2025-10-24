// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using ILogger = Microsoft.Extensions.Logging.ILogger;
using Log = Serilog.Log;

namespace DroidNet.Bootstrap;

/// <summary>
///     Partial containing logging helpers for <see cref="Bootstrapper"/>.
/// <para>
///     Pattern: prefer the final <see cref="ILogger"/> stored in <c>logger</c> when available,
///     otherwise fall back to Serilog's static early logger (used during bootstrap before DI is ready).
/// </para>
/// </summary>
public sealed partial class Bootstrapper
{
    // Final logger instance (populated after the host is built when possible).
    private ILogger? logger;

    [LoggerMessage(EventId = 32001, Level = LogLevel.Debug, Message = "Bootstrapper.Configure called")]
    private static partial void LogConfigureCalledCore(Microsoft.Extensions.Logging.ILogger logger);

    private void LogConfigureCalled()
    {
        if (this.logger is not null)
        {
            LogConfigureCalledCore(this.logger);
        }
        else
        {
            Log.Debug("Bootstrapper.Configure called");
        }
    }

    [LoggerMessage(EventId = 32002, Level = LogLevel.Debug, Message = "Bootstrapper.WithRouting called")]
    private static partial void LogWithRoutingCore(Microsoft.Extensions.Logging.ILogger logger);

    private void LogWithRouting()
    {
        if (this.logger is not null)
        {
            LogWithRoutingCore(this.logger);
        }
        else
        {
            Log.Debug("Bootstrapper.WithRouting called");
        }
    }

    [LoggerMessage(EventId = 32003, Level = LogLevel.Debug, Message = "Bootstrapper.WithMvvm called")]
    private static partial void LogWithMvvmCore(Microsoft.Extensions.Logging.ILogger logger);

    private void LogWithMvvm()
    {
        if (this.logger is not null)
        {
            LogWithMvvmCore(this.logger);
        }
        else
        {
            Log.Debug("Bootstrapper.WithMvvm called");
        }
    }

    [LoggerMessage(EventId = 32004, Level = LogLevel.Debug, Message = "Bootstrapper.WithWinUI called for '{ApplicationType}', lifetimeLinked={IsLifetimeLinked}")]
    private static partial void LogWithWinUICore(Microsoft.Extensions.Logging.ILogger logger, string applicationType, bool isLifetimeLinked);

    private void LogWithWinUI<TApplication>(bool isLifetimeLinked)
    {
        var appType = typeof(TApplication).FullName ?? typeof(TApplication).Name;
        if (this.logger is not null)
        {
            LogWithWinUICore(this.logger, appType, isLifetimeLinked);
        }
        else
        {
            Log.Debug("Bootstrapper.WithWinUI called for '{ApplicationType}', lifetimeLinked={IsLifetimeLinked}", appType, isLifetimeLinked);
        }
    }

    [LoggerMessage(EventId = 32010, Level = LogLevel.Information, Message = "Early services configured (final container assigned)")]
    private static partial void LogEarlyServicesConfiguredCore(Microsoft.Extensions.Logging.ILogger logger);

    private void LogEarlyServicesConfigured()
    {
        if (this.logger is not null)
        {
            LogEarlyServicesConfiguredCore(this.logger);
        }
        else
        {
            Log.Information("Early services configured (final container assigned)");
        }
    }

    [LoggerMessage(EventId = 32011, Level = LogLevel.Information, Message = "Host has been built")]
    private static partial void LogHostBuiltCore(Microsoft.Extensions.Logging.ILogger logger);

    private void LogHostBuilt()
    {
        if (this.logger is not null)
        {
            LogHostBuiltCore(this.logger);
        }
        else
        {
            Log.Information("Host has been built");
        }
    }

    [LoggerMessage(EventId = 32012, Level = LogLevel.Information, Message = "Host starting (Run)")]
    private static partial void LogHostStartingCore(Microsoft.Extensions.Logging.ILogger logger);

    private void LogHostStarting()
    {
        if (this.logger is not null)
        {
            LogHostStartingCore(this.logger);
        }
        else
        {
            Log.Information("Host starting (Run)");
        }
    }

    [LoggerMessage(EventId = 32013, Level = LogLevel.Information, Message = "Host stopped")]
    private static partial void LogHostStoppedCore(Microsoft.Extensions.Logging.ILogger logger);

    private void LogHostStopped()
    {
        if (this.logger is not null)
        {
            LogHostStoppedCore(this.logger);
        }
        else
        {
            Log.Information("Host stopped");
        }
    }
}
