# Oxygen.Examples.TexturedCube

A minimal sample that demonstrates the engine's texture import pipeline with
BC7 compression and HDR environment support. It serves as a **testbed for the
texture importer**, allowing manual control over layout and format selection.

It renders a cube and exercises bindless texture sampling. The sample showcases
the new texture import system including:

- **BC7 compression** for high-quality GPU-compressed textures
- **Mip map generation** with Kaiser/Lanczos filtering
- **HDR environment loading** from equirectangular panoramas (.hdr, .exr)
- **Equirectangular to cubemap conversion** for skybox rendering
- **Multiple skybox layouts** (equirectangular, cross, strip)

## Controls

- Mouse wheel: zoom
- RMB + mouse drag: orbit

## Features

### Texture Loading (Materials/UV Tab)

- **Browse/Load**: Load any PNG, JPG, HDR, or EXR image
- **BC7 compression**: Toggle GPU-compressed BC7 format (smaller VRAM, fast sampling)
- **Generate mips**: Toggle mip map generation for better filtering at distance
- **UV controls**: Adjust UV scale and offset for texture mapping

### HDR Environment (Lighting Tab)

- **Browse/Load skybox**: Load HDR/EXR equirectangular panoramas or cross/strip layouts
- **Layout**: Select input image layout:
  - Equirectangular (2:1) — standard HDR panorama format
  - Horizontal Cross (4×3) — cross layout with faces arranged horizontally
  - Vertical Cross (3×4) — cross layout with faces arranged vertically
  - Horizontal Strip (6×1) — all 6 faces in a row
  - Vertical Strip (1×6) — all 6 faces in a column
- **Output format**: Select output texture format:
  - RGBA8 (LDR) — 8-bit uncompressed
  - RGBA16F (HDR) — 16-bit float for HDR preservation
  - RGBA32F (HDR) — 32-bit float for maximum precision
  - BC7 (LDR) — GPU-compressed with tonemapping
- **Cube face size**: Resolution per face (128–2048, power-of-two only, equirectangular only)
- **Flip Y**: Flip image vertically during decode (enable for standard HDRIs)
- **Sky light controls**: Adjust IBL intensity, diffuse, and specular contributions

## Supported Image Formats

| Format | Type | Description |
| ------ | ---- | ----------- |
| PNG | LDR | Standard 8-bit images |
| JPG | LDR | Compressed 8-bit images |
| BMP | LDR | Uncompressed 8-bit images |
| HDR | HDR | Radiance RGBE format (equirectangular panoramas) |
| EXR | HDR | OpenEXR format (high precision, equirectangular panoramas) |

## Notes

- This example relies on the default bindless sampler created by the D3D12
  backend (sampler heap slot 0).
- BC7 compression is performed at runtime using the bc7enc library.
- HDR environments are tonemapped using ACES when BC7 compression is enabled.
- The equirectangular to cubemap conversion uses Kaiser filtering for quality.
- Cube face size should be power-of-two (128, 256, 512, 1024, 2048).
- For equirectangular panoramas, optimal face size ≈ source height / 2.

## Texture Playground

When the texture index mode is set to **Custom**, you can:

- Enter a texture resource index (non-zero)
- Browse for an image file (PNG, JPG, HDR, EXR)
- Enable BC7 compression for GPU-optimized textures
- Generate mip maps for improved filtering

The sample uses the new texture import pipeline with support for:

- stb_image for LDR formats (PNG, JPG, BMP)
- stb_image for Radiance HDR
- tinyexr for OpenEXR

## Example Usage

### Loading a BC7-Compressed Albedo Texture

1. Set texture mode to "Custom"
2. Click "Browse..." and select a PNG/JPG image
3. Enable "BC7 compression" and "Generate mips"
4. Click "Load PNG"

### Loading an HDR Environment Skybox

1. Go to the "Lighting" tab
2. Click "Browse skybox..." and select an .hdr or .exr file
3. Select "Equirectangular (2:1)" layout (default for most HDRIs)
4. Select output format (RGBA16F/32F for HDR, or BC7/RGBA8 for tonemapped LDR)
5. Set "Cube face size" (512 or 1024 recommended)
   - When using an LDR output format, tonemapping is enabled automatically and
     an Exposure (EV) control is shown.
6. Enable "Flip Y" if the skybox appears upside-down
7. Click "Load skybox"

### Loading a Cross/Strip Layout Skybox

1. Go to the "Lighting" tab
2. Click "Browse skybox..." and select your cross or strip image
3. Select the matching layout (e.g., "Horizontal Cross (4×3)")
4. Select output format
5. Click "Load skybox"
