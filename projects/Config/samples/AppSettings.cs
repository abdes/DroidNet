// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Config.Example;

[SuppressMessage("Usage", "CA1812:Avoid uninstantiated internal classes", Justification = "Instantiated by DI container at runtime")]
internal sealed class AppSettings : IAppSettings
{
    public string ApplicationName { get; set; } = "DroidNet Config Sample";

    public string LoggingLevel { get; set; } = "Warning";

    public bool EnableExperimental { get; set; }
}
