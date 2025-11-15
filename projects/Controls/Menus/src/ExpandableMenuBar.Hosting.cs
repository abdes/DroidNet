// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Menus;

/// <content>
///     Host customization plumbing for <see cref="ExpandableMenuBar"/>.
/// </content>
public sealed partial class ExpandableMenuBar
{
    private static readonly Func<ICascadedMenuHost> DefaultHostFactory = static () => new FlyoutMenuHost();

    /// <summary>
    ///     Gets or sets the factory responsible for producing cascading menu hosts for the embedded <see cref="MenuBar"/>.
    /// </summary>
    /// <remarks>
    ///     Assign a custom factory before or after template application; the value is forwarded to the inner menu bar
    ///     when available and cached otherwise.
    /// </remarks>
    internal Func<ICascadedMenuHost> HostFactory
    {
        get
        {
            if (this.hostFactory is { } factory)
            {
                return factory;
            }

            if (this.innerMenuBar is { } menuBar)
            {
                this.hostFactory = menuBar.HostFactory;
                return this.hostFactory;
            }

            return DefaultHostFactory;
        }

        set
        {
            ArgumentNullException.ThrowIfNull(value);

            this.hostFactory = value;
            this.SyncHostFactory();
        }
    }

    private void SyncHostFactory()
    {
        if (this.innerMenuBar is not { } menuBar)
        {
            return;
        }

        if (this.hostFactory is { } factory)
        {
            menuBar.HostFactory = factory;
        }
        else
        {
            this.hostFactory = menuBar.HostFactory;
        }
    }
}
