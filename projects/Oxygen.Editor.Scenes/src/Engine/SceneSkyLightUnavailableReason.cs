// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Engine;

/// <summary>Why the engine's sky light casts no light.</summary>
public enum SceneSkyLightUnavailableReason
{
    /// <summary>The sky light is available.</summary>
    None = 0,

    /// <summary>No cubemap is selected.</summary>
    MissingCubemap = 1,

    /// <summary>The cubemap failed to load.</summary>
    ResourceResolveFailed = 2,

    /// <summary>The selected texture is not a cubemap.</summary>
    NotTextureCube = 3,

    /// <summary>The cubemap's format cannot light the scene.</summary>
    UnsupportedFormat = 4,

    /// <summary>Image-based lighting failed to process.</summary>
    ProcessingFailed = 5,

    /// <summary>The cubemap's GPU products are still being prepared.</summary>
    GpuProductsPending = 6,
}
