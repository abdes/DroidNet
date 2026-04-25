// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     JSON-serializable editor engine settings.
/// </summary>
public sealed class EngineSettings : IEngineSettings
{
    /// <summary>
    ///     The configuration section name in the editor settings file.
    /// </summary>
    public const string ConfigSectionName = "EngineSettings";

    /// <inheritdoc/>
    public PlatformSettings Platform { get; set; } = new();

    /// <inheritdoc/>
    public EngineCoreSettings Engine { get; set; } = new();

    /// <inheritdoc/>
    public TimingSettings Timing { get; set; } = new();

    /// <inheritdoc/>
    public RendererSettings Renderer { get; set; } = new();

    /// <inheritdoc/>
    public GraphicsSettings Graphics { get; set; } = new();
}
