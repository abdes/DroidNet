// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;

namespace DroidNet.Docking.Controls;

/// <summary>
/// Represents a message that indicates a request to enter docking mode for a specified dock.
/// </summary>
/// <param name="value">The <see cref="IDock"/> instance that is to enter docking mode.</param>
/// <remarks>
/// This message is used within the docking framework to signal that a dock should enter docking mode.
/// It carries the <see cref="IDock"/> instance that is to be docked.
/// <para>
/// <strong>Example Usage:</strong>
/// <code><![CDATA[
/// IDock dock = ...;
/// var message = new EnterDockingModeMessage(dock);
/// StrongReferenceMessenger.Default.Send(message);
/// ]]></code>
/// </para>
/// </remarks>
public class EnterDockingModeMessage(IDock value) : ValueChangedMessage<IDock>(value)
{
}
