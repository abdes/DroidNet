// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Core.Services;

using DroidNet.Config;

public interface IOxygenPathFinder : IPathFinder
{
    public string PersonalProjects { get; }

    public string LocalProjects { get; }
}
