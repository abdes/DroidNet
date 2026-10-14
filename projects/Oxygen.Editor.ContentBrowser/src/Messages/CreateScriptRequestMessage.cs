// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>
/// Runs New Script from another pane, such as a script slot in the inspector. The reply is the new script file, or
/// <see langword="null"/> when the user cancelled or the file could not be written.
/// </summary>
public sealed class CreateScriptRequestMessage : AsyncRequestMessage<string?>;
