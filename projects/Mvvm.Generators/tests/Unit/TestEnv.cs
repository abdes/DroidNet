// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.TestHelpers;

namespace DroidNet.Mvvm.Generators.Tests;

[TestClass]
[TestCategory("Test Environment")]
[ExcludeFromCodeCoverage]
public class TestEnv : CommonTestEnv
{
    [AssemblyCleanup]
    public static void Close()
    {
    }
}
