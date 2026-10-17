// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Editor settings used to create the native engine; logging verbosity also applies while it runs.
/// </summary>
public interface IEngineStartupSettings
{
    /// <summary>
    ///     Gets or sets platform settings.
    /// </summary>
    public PlatformSettings Platform { get; set; }

    /// <summary>
    ///     Gets or sets core engine settings.
    /// </summary>
    public EngineCoreSettings Engine { get; set; }

    /// <summary>
    ///     Gets or sets timing settings.
    /// </summary>
    public TimingSettings Timing { get; set; }

    /// <summary>
    ///     Gets or sets renderer settings.
    /// </summary>
    public RendererSettings Renderer { get; set; }

    /// <summary>
    ///     Gets or sets graphics backend settings.
    /// </summary>
    public GraphicsSettings Graphics { get; set; }

    /// <summary>
    ///     Gets or sets the native logging verbosity, between <see cref="EngineConstants.MinLoggingVerbosity"/>
    ///     and <see cref="EngineConstants.MaxLoggingVerbosity"/>.
    /// </summary>
    public int LoggingVerbosity { get; set; }
}
