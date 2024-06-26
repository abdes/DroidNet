// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

public class ContextEventArgs(RouterContext? context) : EventArgs
{
    public RouterContext? Context => context;
}
