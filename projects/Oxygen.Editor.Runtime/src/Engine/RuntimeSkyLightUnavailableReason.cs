// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Why the rendered sky light has no image-based lighting; mirrors the native reason.</summary>
public enum RuntimeSkyLightUnavailableReason
{
    /// <summary>Image-based lighting is available, or the sky light is disabled.</summary>
    None = 0,

    /// <summary>A specified-cubemap source has no cubemap.</summary>
    MissingCubemap = 1,

    /// <summary>The cubemap failed to load.</summary>
    ResourceResolveFailed = 2,

    /// <summary>The texture is not a six-face cube.</summary>
    NotTextureCube = 3,

    /// <summary>The cubemap is not a float (HDR) format.</summary>
    UnsupportedFormat = 4,

    /// <summary>Image-based lighting processing failed.</summary>
    ProcessingFailed = 5,

    /// <summary>The cubemap is still uploading.</summary>
    GpuProductsPending = 6,
}
