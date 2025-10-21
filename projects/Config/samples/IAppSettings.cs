// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config.Example;

internal interface IAppSettings
{
    public string ApplicationName { get; set; }

    public string LoggingLevel { get; set; }

    public bool EnableExperimental { get; set; }
}
