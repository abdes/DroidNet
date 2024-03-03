// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;

[ExcludeFromCodeCoverage]
internal sealed class SimpleDock : Dock;

internal sealed class NoMinimizeDock : Dock
{
    public override bool CanMinimize => false;
}

internal sealed class NoCloseDock : Dock
{
    public override bool CanClose => false;
}

internal sealed class SimpleCenterDock : Dock
{
    public override bool CanMinimize => false;

    public override bool CanClose => false;
}
