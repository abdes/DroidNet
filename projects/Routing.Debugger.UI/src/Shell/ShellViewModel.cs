// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.UI;

public partial class ShellViewModel : AbstractOutletContainer
{
    [ObservableProperty]
    private string? url;

    protected override Dictionary<string, object?> Outlets { get; } = new(StringComparer.OrdinalIgnoreCase)
    {
        { "dock", null },
    };
}
