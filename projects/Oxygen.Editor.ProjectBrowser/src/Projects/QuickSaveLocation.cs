// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

public class QuickSaveLocation(string name, string path)
{
    public string Path { get; set; } = path;

    public string Name { get; set; } = name;
}
