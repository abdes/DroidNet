// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Allows a host to register importers.
/// </summary>
public interface IImportPlugin
{
    /// <summary>
    /// Registers importers for this plugin.
    /// </summary>
    /// <param name="registration">The registration sink.</param>
    public void Register(ImportPluginRegistration registration);
}
