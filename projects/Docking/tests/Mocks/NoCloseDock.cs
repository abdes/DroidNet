// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;

/// <summary>
/// A test dock class that cannot be closed.
/// </summary>
[ExcludeFromCodeCoverage]
internal sealed partial class NoCloseDock : Dock
{
    public override bool CanClose => false;
}
