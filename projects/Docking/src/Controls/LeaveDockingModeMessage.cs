// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

/// <summary>
/// Represents a message that indicates a request to leave docking mode.
/// </summary>
/// <remarks>
/// This message is used within the docking framework to signal that a dock should leave docking mode.
/// It does not carry any additional data and serves as a simple notification.
/// <para>
/// <strong>Example Usage:</strong>
/// <code><![CDATA[
/// var message = new LeaveDockingModeMessage();
/// StrongReferenceMessenger.Default.Send(message);
/// ]]></code>
/// </para>
/// </remarks>
public class LeaveDockingModeMessage;
