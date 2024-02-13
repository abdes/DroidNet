// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

public class NavigationFailedException(string because, Exception? innerException = null)
    : Exception($"navigation failed because: {because}", innerException)
{
    // TODO(abdes): add extra information to this exception
}
