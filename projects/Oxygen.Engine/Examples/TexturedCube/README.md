# Oxygen.Examples.TexturedCube

A minimal sample inspired by the InputSystem example.

It renders a cube and exercises bindless texture sampling by intentionally
setting the cube material's `base_color_texture` to an invalid resource index.
The runtime TextureBinder resolves that to the engine error checkerboard.

## Controls

- Mouse wheel: zoom
- RMB + mouse drag: orbit

## Notes

- This example relies on the default bindless sampler created by the D3D12
  backend (sampler heap slot 0).

## Texture Playground

When the texture index mode is set to **Custom**, you can:

- Enter a texture resource index (non-zero)
- Browse for a `.png` and load it

The sample decodes the PNG with Windows WIC (no third-party image library) and
uploads it as an RGBA8 texture, overriding that resource index at runtime.
