// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using Destructurama;
using DryIoc;
using Microsoft.Extensions.Logging;
using Serilog;
using Serilog.Extensions.Logging;
using Serilog.Templates;

namespace DroidNet.Routing.Tests;

/// <summary>
/// Contains global initialization for Verify, the IoC container and logging.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(GlobalTestInitializer))]
public class GlobalTestInitializer : VerifyBase
{
    internal static IContainer Container { get; set; } = new Container();

    [AssemblyInitialize]
    public static void Init(TestContext context)
    {
        _ = context; // unused

        ConfigureLogging(Container);
        ConfigureVerify();
    }

    [AssemblyCleanup]
    public static void Close()
    {
        Log.Information("Test session ended");
        Serilog.Log.CloseAndFlush();
    }

    [TestMethod]
    public Task VerifyConventions_Satisfied() =>
        VerifyChecks.Run();

    private static void ConfigureLogging(IContainer container)
    {
        Log.Logger = new LoggerConfiguration()
            .Destructure.UsingAttributes()
            .MinimumLevel.Error()
            .Enrich.FromLogContext()
            .WriteTo.Debug(
                new ExpressionTemplate(
                    "[{@t:HH:mm:ss} {@l:u3} ({Substring(SourceContext, LastIndexOf(SourceContext, '.') + 1)})] {@m:lj}\n{@x}",
                    new CultureInfo("en-US")))
            /*.WriteTo.Seq("http://localhost:5341/")*/
            .CreateLogger();

        Log.Information("Test session started");

        var loggerFactory = new SerilogLoggerFactory(Log.Logger);
        try
        {
            container.RegisterInstance<ILoggerFactory>(loggerFactory);
            loggerFactory = null; // prevent disposal
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

    private static void ConfigureVerify()
    {
        VerifierSettings.TreatAsString<OutletName>();
        VerifierSettings.IgnoreMember<OutletName>(o => o.IsPrimary);
        VerifierSettings.IgnoreMember<OutletName>(o => o.IsNotPrimary);
        VerifierSettings.IgnoreMembers<IRoute>(
            o => o.Outlet,
            o => o.Matcher,
            o => o.Children);
        VerifierSettings.IgnoreMember<IActiveRoute>("Root");
        VerifierSettings.IgnoreMember<IActiveRoute>("Siblings");
        VerifierSettings.IgnoreMember<IActiveRoute>("Parent");

        VerifyDiffPlex.Initialize();
    }
}
