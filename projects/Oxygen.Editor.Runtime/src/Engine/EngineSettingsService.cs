// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Config;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Settings service for native engine startup configuration.
/// </summary>
public sealed class EngineSettingsService(SettingsManager manager, ILoggerFactory? factory = null)
    : SettingsService<IEngineSettings>(manager, factory), IEngineSettings
{
    private PlatformSettings platform = new();
    private EngineCoreSettings engine = new();
    private TimingSettings timing = new();
    private RendererSettings renderer = new();
    private GraphicsSettings graphics = new();

    /// <inheritdoc/>
    public PlatformSettings Platform
    {
        get => this.platform;
        set => _ = this.SetField(ref this.platform, value ?? new PlatformSettings());
    }

    /// <inheritdoc/>
    public EngineCoreSettings Engine
    {
        get => this.engine;
        set => _ = this.SetField(ref this.engine, value ?? new EngineCoreSettings());
    }

    /// <inheritdoc/>
    public TimingSettings Timing
    {
        get => this.timing;
        set => _ = this.SetField(ref this.timing, value ?? new TimingSettings());
    }

    /// <inheritdoc/>
    public RendererSettings Renderer
    {
        get => this.renderer;
        set => _ = this.SetField(ref this.renderer, value ?? new RendererSettings());
    }

    /// <inheritdoc/>
    public GraphicsSettings Graphics
    {
        get => this.graphics;
        set => _ = this.SetField(ref this.graphics, value ?? new GraphicsSettings());
    }

    /// <inheritdoc/>
    public override string SectionName => EngineSettings.ConfigSectionName;

    /// <inheritdoc/>
    public override Type SettingsType => typeof(EngineSettings);

    /// <inheritdoc/>
    public override SettingsSectionMetadata SectionMetadata { get; set; } = new()
    {
        SchemaVersion = "20260425",
        Service = typeof(EngineSettingsService).FullName,
    };

    /// <inheritdoc/>
    protected override object GetSettingsSnapshot()
        => new EngineSettings
        {
            Platform = this.platform,
            Engine = this.engine,
            Timing = this.timing,
            Renderer = this.renderer,
            Graphics = this.graphics,
        };

    /// <inheritdoc/>
    protected override void UpdateProperties(IEngineSettings newSettings)
    {
        ArgumentNullException.ThrowIfNull(newSettings);

        this.Platform = newSettings.Platform;
        this.Engine = newSettings.Engine;
        this.Timing = newSettings.Timing;
        this.Renderer = newSettings.Renderer;
        this.Graphics = newSettings.Graphics;
    }

    /// <inheritdoc/>
    protected override IEngineSettings CreateDefaultSettings() => new EngineSettings();
}
