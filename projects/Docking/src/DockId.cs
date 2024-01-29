// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

public readonly struct DockId(int value)
{
    public int Value { get; } = value;

    public override string ToString() => $"{this.Value}";
}
