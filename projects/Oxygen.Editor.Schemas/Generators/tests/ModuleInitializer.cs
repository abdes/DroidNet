// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

namespace Oxygen.Editor.Schemas.Generators.Tests;

/// <summary>Module level initialization code for the test frameworks.</summary>
[ExcludeFromCodeCoverage]
public static class ModuleInitializer
{
    /// <summary>Lets <see href="https://github.com/VerifyTests/Verify.SourceGenerators">Verify</see> snapshot generator runs.</summary>
    [ModuleInitializer]
    public static void Init() => VerifySourceGenerators.Initialize();
}
