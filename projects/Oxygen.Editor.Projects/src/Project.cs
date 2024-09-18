// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

public class Project(IProjectInfo info) : IProject
{
    public IList<Scene> Scenes { get; } = [];

    public IProjectInfo ProjectInfo => info;
}
