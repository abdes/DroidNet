// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;

[ExcludeFromCodeCoverage]
internal sealed class NoCloseDock : Dock
{
    public override bool CanClose => false;
}
