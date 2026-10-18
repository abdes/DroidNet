// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>
/// Message sent when a material source should open in the material editor.
/// </summary>
/// <param name="material">The material to open.</param>
/// <param name="title">The material document title.</param>
public sealed class OpenMaterialRequestMessage(AssetPath material, string title) : RequestMessage<bool>
{
    /// <summary>
    /// Gets the material to open.
    /// </summary>
    public AssetPath Material { get; } = material;

    /// <summary>
    /// Gets the material document title.
    /// </summary>
    public string Title { get; } = title;
}
