// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>
/// Message sent when a new material descriptor should be created and opened.
/// </summary>
/// <param name="materialUri">The target material source asset URI.</param>
/// <param name="title">The material display name.</param>
public sealed class CreateMaterialRequestMessage(Uri materialUri, string title) : RequestMessage<bool>
{
    /// <summary>
    /// Gets the target material source asset URI.
    /// </summary>
    public Uri MaterialUri { get; } = materialUri;

    /// <summary>
    /// Gets the material display name.
    /// </summary>
    public string Title { get; } = title;
}
