// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

public readonly struct DockId(int value)
{
    public int Value { get; } = value;

    public bool Equals(DockId other) => this.Value == other.Value;

    public override int GetHashCode() => this.Value;

    public override string ToString() => $"{this.Value}";
}
