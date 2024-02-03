// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

public interface IDockable
{
    string Id { get; }

    string Title { get; }

    string MinimizedTitle { get; }

    string TabbedTitle { get; }

    object? ViewModel { get; }
}
