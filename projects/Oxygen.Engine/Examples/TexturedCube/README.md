# Oxygen.Examples.TexturedCube

A minimal sample inspired by the InputSystem example that demonstrates the
engine's texture import pipeline with BC7 compression and HDR environment support.

It renders a cube and exercises bindless texture sampling. The sample showcases
the new texture import system including:

- **BC7 compression** for high-quality GPU-compressed textures
- **Mip map generation** with Kaiser/Lanczos filtering
- **HDR environment loading** from equirectangular panoramas (.hdr, .exr)
- **Equirectangular to cubemap conversion** for skybox rendering

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

- **Browse/Load skybox**: Load HDR/EXR equirectangular panorama files
- **HDR environment checkbox**: Enable equirectangular → cubemap conversion
- **BC7 (bake to LDR)**: Tonemap HDR to LDR and compress with BC7
- **Cube face size**: Configure cubemap resolution (128–2048)
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
3. Enable "HDR environment"
4. Optionally enable "BC7 (bake to LDR)" for compressed skybox
5. Adjust "Cube face size" as needed (512 is a good default)
6. Click "Load skybox"
