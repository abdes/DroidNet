// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>
/// Message sent when a material source should open in the material editor.
/// </summary>
/// <param name="materialUri">The material source asset URI.</param>
/// <param name="title">The material document title.</param>
public sealed class OpenMaterialRequestMessage(Uri materialUri, string title) : RequestMessage<bool>
{
    /// <summary>
    /// Gets the material source asset URI.
    /// </summary>
    public Uri MaterialUri { get; } = materialUri;

    /// <summary>
    /// Gets the material document title.
    /// </summary>
    public string Title { get; } = title;
}
