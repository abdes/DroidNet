// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Demo;

using System.Diagnostics.CodeAnalysis;

[ExcludeFromCodeCoverage]
public sealed class ExampleSettings
{
    public const string Section = "Example";

    public string? Greeting { get; init; }
}
