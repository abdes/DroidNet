// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators;

using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

/// <summary>Module level initialization code for the test frameworks.</summary>
[ExcludeFromCodeCoverage]
public static class ModuleInitializer
{
    /// <summary>Initialize the <see href="https://github.com/VerifyTests">`Verify`</see> test framework.</summary>
    [ModuleInitializer]
    public static void Init()
    {
        VerifyDiffPlex.Initialize();
        VerifySourceGenerators.Initialize();
    }
}
