// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TestHelpers;

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using Destructurama;
using DryIoc;
using Microsoft.Extensions.Logging;
using Serilog;
using Serilog.Extensions.Logging;
using Serilog.Templates;

/// <summary>
/// Contains global initialization for Verify, the IoC container and logging.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Test Environment")]
public class CommonTestEnv : VerifyBase
{
    public static IContainer TestContainer { get; set; } = new Container();

    [TestMethod]
    public Task VerifyConventions_Satisfied() =>
        VerifyChecks.Run();

    protected static void ConfigureLogging(IContainer container)
    {
        Log.Logger = new LoggerConfiguration()
            .Destructure.UsingAttributes()
            .MinimumLevel.Information()
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

    protected static void ConfigureVerify() => VerifyDiffPlex.Initialize();
}