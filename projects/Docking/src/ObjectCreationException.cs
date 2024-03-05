// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

/// <summary>
/// Internal exception used to report a failure in creating an instance of a factory-managed type, such as dockables and docks.
/// </summary>
/// <param name="type">The type of the object which creation failed.</param>
public sealed class ObjectCreationException(Type type, Exception? ex = null)
    : ApplicationException($"could not create an instance of {type.FullName}", ex);
