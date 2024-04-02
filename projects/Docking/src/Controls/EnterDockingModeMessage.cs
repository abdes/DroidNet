// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using CommunityToolkit.Mvvm.Messaging.Messages;

public class EnterDockingModeMessage(IDock value) : ValueChangedMessage<IDock>(value);

public class LeaveDockingModeMessage;
