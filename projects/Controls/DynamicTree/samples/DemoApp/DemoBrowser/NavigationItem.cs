// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DemoBrowser;

public class NavigationItem(string path, string text, Type target)
{
    public string Path { get; } = path;

    public string Text { get; } = text;

    public Type TargetViewModel { get; } = target;
}
