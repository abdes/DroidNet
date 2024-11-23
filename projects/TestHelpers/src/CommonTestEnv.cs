// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using Destructurama;
using DryIoc;
using Microsoft.Extensions.Logging;
using Serilog;
using Serilog.Core;
using Serilog.Events;
using Serilog.Extensions.Logging;
using Serilog.Templates;

namespace DroidNet.TestHelpers;

/// <summary>
/// Contains global initialization for Verify, the IoC container, and logging.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Test Environment")]
public class CommonTestEnv : VerifyBase
{
    /// <summary>
    /// Gets or sets the IoC container used for testing.
    /// </summary>
    public static IContainer TestContainer { get; set; } = new Container();

    /// <summary>
    /// Gets the logging level switch for the Seriog logger, which can be used to change the logging level at runtime.
    /// </summary>
    /// <example>
    /// <code><![CDATA[
    /// public void AddProject(ProjectState project)
    /// {
    ///     CommonTestEnv.LoggingLevelSwitch.MinimumLevel = LogEventLevel.Debug;
    ///     _ = context.Projects.Add(project);
    ///     _ = context.SaveChanges();
    ///     CommonTestEnv.LoggingLevelSwitch.MinimumLevel = LogEventLevel.Warning;
    /// }
    /// ]]></code>
    /// </example>
    public static LoggingLevelSwitch LoggingLevelSwitch { get; } = new(LogEventLevel.Warning);

    /// <summary>
    /// Verifies that the conventions are satisfied.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    [TestMethod]
    public Task VerifyConventions_Satisfied() => VerifyChecks.Run();

    /// <summary>
    /// Configures logging for the specified IoC container.
    /// </summary>
    /// <param name="container">The IoC container to configure logging for.</param>
    protected static void ConfigureLogging(IContainer container)
    {
        Log.Logger = new LoggerConfiguration()
            .Destructure.UsingAttributes()
            .MinimumLevel.ControlledBy(LoggingLevelSwitch)
            .Enrich.FromLogContext()
            .WriteTo.Debug(
                new ExpressionTemplate(
                    "[{@t:HH:mm:ss} {@l:u3} ({Substring(SourceContext, LastIndexOf(SourceContext, '.') + 1)})] {@m:lj}\n{@x}",
                    new CultureInfo("en-US")))
            /*.WriteTo.Seq("http://localhost:5341/")*/
            .CreateLogger();

        var loggerFactory = new SerilogLoggerFactory(Log.Logger);
        try
        {
            container.RegisterInstance<ILoggerFactory>(loggerFactory);
            loggerFactory = null; // Dispose ownership transferred to the DryIoc Container
        }
        finally
        {
            loggerFactory?.Dispose();
        }

        container.Register(
            Made.Of(
                _ => ServiceInfo.Of<ILoggerFactory>(),
                f => f.CreateLogger(null!)),
            setup: Setup.With(condition: r => r.Parent.ImplementationType == null));

        container.Register(
            Made.Of(
                _ => ServiceInfo.Of<ILoggerFactory>(),
                f => f.CreateLogger(Arg.Index<Type>(0)),
                r => r.Parent.ImplementationType),
            setup: Setup.With(condition: r => r.Parent.ImplementationType != null));
    }

    /// <summary>
    /// Configures Verify for the test environment.
    /// </summary>
    protected static void ConfigureVerify() => VerifyDiffPlex.Initialize();
}
