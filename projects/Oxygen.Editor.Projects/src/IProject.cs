// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using System.Collections.Generic;

public interface IProject
{
    IProjectInfo ProjectInfo { get; }

    IList<Scene> Scenes { get; }
}
