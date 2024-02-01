// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Collections.ObjectModel;

public interface IDockGroup
{
    public ReadOnlyObservableCollection<IDock> Docks { get; }

    bool IsHorizontal { get; }

    bool IsVertical { get; }

    bool IsEmpty { get; }

    Orientation Orientation { get; }

    IDockGroup? First { get; }

    IDockGroup? Second { get; }
}