// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Mvvm.Generators.Tests.Demo;
using DroidNet.TestHelpers;
using DryIoc;
using Serilog;

namespace DroidNet.Mvvm.Generators.Tests;

[TestClass]
[TestCategory("Test Environment")]
[ExcludeFromCodeCoverage]
public class TestEnv : CommonTestEnv
{
    [AssemblyInitialize]
    public static void Init(TestContext context)
    {
        _ = context; // unused

        ConfigureLogging(TestContainer);
        TestContainer.Register<IViewFor<DemoViewModel>, DemoView>();
        Log.Information("Test session started");

        ConfigureVerify();
    }

    [AssemblyCleanup]
    public static void Close()
    {
        Log.Information("Test session ended");
        Log.CloseAndFlush();
    }
}
