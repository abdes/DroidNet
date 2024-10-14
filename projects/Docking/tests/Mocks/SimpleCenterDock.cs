// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;

/// <summary>
/// A test dock suitable for the center dock group.
/// </summary>
[ExcludeFromCodeCoverage]
internal sealed partial class SimpleCenterDock : Dock
{
    public override bool CanMinimize => false;

    public override bool CanClose => false;
}
