// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Windows;

/// <summary>
///     What a participant of closing a window answers for. Participants prepare, commit and finalize a close in this
///     order.
/// </summary>
public enum WindowCloseLevel
{
    /// <summary>The documents of the window, which ask about their unsaved changes first.</summary>
    Document,

    /// <summary>The window itself, such as its placement.</summary>
    Window,

    /// <summary>The project session whose window closes.</summary>
    Session,

    /// <summary>The editor, which a close of its last window ends.</summary>
    Application,
}
