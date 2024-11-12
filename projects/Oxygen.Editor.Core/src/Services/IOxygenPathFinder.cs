// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Core.Services;

using DroidNet.Config;

public interface IOxygenPathFinder : IPathFinder
{
    string PersonalProjects { get; }

    string LocalProjects { get; }
}
