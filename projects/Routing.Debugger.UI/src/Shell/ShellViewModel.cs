// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

/// <summary>
/// A ViewModel fpr the debugger shell.
/// </summary>
public class ShellViewModel(IRouter router) : AbstractOutletContainer, IDisposable
{
    public IRouter Router { get; } = router;

    protected override Dictionary<string, object?> Outlets { get; } = new(StringComparer.OrdinalIgnoreCase)
    {
        { "dock", null },
    };

    public void Dispose()
    {
        foreach (var entry in this.Outlets)
        {
            if (entry.Value is IDisposable resource)
            {
                resource.Dispose();
            }
        }

        GC.SuppressFinalize(this);
    }
}
