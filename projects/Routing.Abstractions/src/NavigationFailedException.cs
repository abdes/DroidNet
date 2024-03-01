// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Exception thrown when a <see cref="IRouter">router</see> navigation fails.
/// </summary>
/// <param name="because">A descriptive message of the raison of the failure.</param>
/// <param name="innerException">An optional exception at the origin of this one.</param>
public class NavigationFailedException(string because, Exception? innerException = null)
    : ApplicationException($"navigation failed because: {because}", innerException)
{
    // TODO(abdes): add extra information to this exception
}
