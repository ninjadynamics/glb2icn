# glb2icn

`glb2icn` is a PC-side offline converter that turns a binary glTF file (`.glb`)
into a PS2 memory-card save **icon** (`.icn`) — the 3D model the PS2 BIOS browser
spins next to a save. In the same spirit as [`dcmesh`](../dcmesh): one small C
executable, `cgltf` for GLB loading, plus `stb_image` / `stb_image_resize2` for
the base-color texture.

The companion `icon.sys` (title, background, lighting, and the `icon.icn`
filename) is written by the game at save time (`playstation2.c`); this tool only
produces the `.icn` model+texture.

## Requirements

- `gcc`, `make`
- `cgltf.h` — reused from dcmesh's vendored copy by default
  (`../dcmesh/meshoptimizer/extern/cgltf.h`), or clone
  <https://github.com/jkuhlmann/cgltf> and pass `CGLTF_DIR=`.
- `stb_image.h` + `stb_image_resize2.h` — from <https://github.com/nothings/stb>.
  Note the **v2** resize header (`stb_image_resize2.h`), not the old one.

```sh
git clone https://github.com/nothings/stb
```

## Build

```sh
cd tools/glb2icn
make
# or: make STB_DIR=/path/to/stb CGLTF_DIR=/path/to/cgltf
```

Output is a single executable: `glb2icn` (`glb2icn.exe` on Windows).

## Usage

```sh
./glb2icn input.glb [output.icn] [options]
```

If the output path is omitted it is written next to the input with a `.icn`
extension.

Options:

| Option | Meaning |
|--------|---------|
| `--scale F`     | Model scale (world units). Default: auto-fit longest axis to ±1.0. |
| `--noflipy`     | Keep glTF +Y-up as-is. Default flips Y (PS2 browser shows it upright). |
| `--rgba R G B A`| Solid fallback texture color (0–255) when the GLB has no base-color texture. |

Example:

```sh
./glb2icn ../../assets/dc/logo.glb logo.icn
```

## Format notes

- Emits one static animation shape: `header(20) | vertices(N*24) | anim(20+16+8) | texture(128*128*2)`.
- Vertices are a flat triangle list (`N` = a multiple of 3); positions/normals/UVs
  are 16-bit fixed-point (÷4096); vertex color is white so the texture + lighting
  show through.
- Texture is the first material's base-color image, resized to 128×128 and packed
  as BGR555 (uncompressed, `tex_type 0x07`).
- The BIOS chokes on very large icons; keep models to a few thousand triangles
  (a warning is printed past ~12k verts). `dcmesh`-style simplification is out of
  scope here — decimate the model upstream if needed.

## Wiring it into the game

The game currently generates a **procedural cube** icon at runtime
(`ps2_build_cube_icn` in `playstation2.c`). To ship a real model instead:
1. `./glb2icn assets/dc/logo.glb assets/ps2/icon.icn`
2. Embed `icon.icn` via the PS2 embed registry (the `tex_assets.s`/`incbin`
   pattern) and have `ps2_mc_write_browser_icon` write that blob instead of the
   procedural cube. (Follow-up; not wired yet.)
