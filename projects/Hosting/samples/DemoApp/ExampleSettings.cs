// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Hosting.Demo;

/// <summary>
/// Represents the settings for the Example section.
/// </summary>
[ExcludeFromCodeCoverage]
[SuppressMessage("Microsoft.Performance", "CA1812:AvoidUninstantiatedInternalClasses", Justification = "This class is instantiated via configuration binding.")]
internal sealed class ExampleSettings
{
    /// <summary>
    /// The section name in the configuration.
    /// </summary>
    public const string Section = "Example";

    /// <summary>
    /// Gets the greeting message.
    /// </summary>
    public string? Greeting { get; init; }
}
