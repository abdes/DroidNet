// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple.Navigation;

public class NavigationItem(string path, string text, string icon, string accessKey, Type target)
{
    public string Path { get; } = path;

    public string Text { get; } = text;

    public string Icon { get; } = icon;

    public string AccessKey { get; } = accessKey;

    public Type TargetViewModel { get; } = target;
}
