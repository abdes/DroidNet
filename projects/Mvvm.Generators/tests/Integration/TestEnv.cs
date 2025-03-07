// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.TestHelpers;
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
        Log.Information("Test session ended");

        ConfigureVerify();
    }

    [AssemblyCleanup]
    public static void Close()
    {
        Log.Information("Test session ended");
        Log.CloseAndFlush();
    }
}
