// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Globalization;

public readonly record struct DockId(uint Value)
{
    public override string ToString() => this.Value.ToString(CultureInfo.InvariantCulture);
}
