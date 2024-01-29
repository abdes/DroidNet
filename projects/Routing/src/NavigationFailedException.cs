// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

public class NavigationFailedException(Exception ex)
    : Exception(
        "Navigation failed due to another error",
        ex)
{
    // TODO(abdes): add extra information to this exception
}
