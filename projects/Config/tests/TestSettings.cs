// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Config.Tests;

[ExcludeFromCodeCoverage]
public sealed class TestSettings
{
    public string FooString { get; init; } = "DefaultFoo";

    public int BarNumber { get; init; } = 42;
}
