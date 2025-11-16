// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Strategy interface for discovering and providing setting descriptors.
/// </summary>
public interface IDescriptorProvider
{
    /// <summary>
    /// Enumerates all available setting descriptors.
    /// </summary>
    /// <returns>An enumerable of setting descriptors.</returns>
    public IEnumerable<ISettingDescriptor> EnumerateDescriptors();
}
