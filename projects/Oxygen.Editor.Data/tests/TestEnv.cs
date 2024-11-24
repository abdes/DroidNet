// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.TestHelpers;

namespace Oxygen.Editor.Data.Tests;

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
    }

    [AssemblyCleanup]
    public static void Close()
    {
    }
}
