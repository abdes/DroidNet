// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

public class QuickSaveLocation
{
    public QuickSaveLocation(string name, string path)
    {
        this.Path = path;
        this.Name = name;
    }

    public string Path { get; set; }

    public string Name { get; set; }
}
