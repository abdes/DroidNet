// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

/// <summary>
/// Specifies the interface of a dockable entity, i.e. something that can be embedded in a dock and docked with a docker.
/// </summary>
public partial interface IDockable : IDisposable
{
    event Action? OnDisposed;

    string Id { get; }

    string Title { get; set; }

    string MinimizedTitle { get; set; }

    string TabbedTitle { get; set; }

    Width PreferredWidth { get; set; }

    Height PreferredHeight { get; set; }

    object? ViewModel { get; }

    IDock? Owner { get; }

    bool IsActive { get; }
}
