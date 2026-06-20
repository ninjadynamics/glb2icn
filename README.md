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
| `--cube`        | Ignore the mesh; emit a **textured cube** (36 verts) showing the image on every face. Implies `--noflipy`. |
| `--flat`        | Ignore the mesh; emit a flat double-sided **textured card**. Implies `--noflipy`. |
| `--tex FILE`    | Take the texture from a standalone image file (PNG/JPG/…) instead of the GLB's base-color. With `--cube`/`--flat` the GLB input becomes optional (`glb2icn --cube --tex icon.png out.icn`). |
| `--scale F`     | Model scale (world units). Default: auto-fit longest axis to ±1.0. |
| `--yoff F`      | Shift the model along the written Y (post-scale). Needed to center it on the BIOS camera target — see below. |
| `--aspect F`    | Extra X stretch (default 1.0). The BIOS renders the icon in true 3D perspective, so a cube is *not* squished by display PAR — leave this at 1.0 unless you have a reason. |

### Centering and size (the BIOS icon camera)

These are not guesswork. The PS2 BIOS icon view is reproduced exactly by the
[mymcplus](https://github.com/thestr4ng3r/mymcplus) renderer:

- Vertex shader: `pos = (raw / 4096.0) * vec3(1.0, -1.0, -1.0)` — **Y is negated**.
- The model is rotated/zoomed about the camera target **(0, 2.5, 0)**, viewed from
  distance **5.0** with an **80° fov**.

Consequences for this tool's output (which writes `raw = value * 4096`):

- **Centering:** the camera target maps to a *written* Y of `-2.5` (the shader
  negates it). A model centered at the origin therefore appears *below* the
  pivot and zooms about its base. Pass `--yoff -2.5` to move the center onto the
  target. (This is why `assets/ps2/icon.png` → cube uses `--yoff -2.5`.)
- **Size:** half-view at the target ≈ `5·tan(40°) ≈ 4.2`. Allowing for the spin
  bringing the near face closer, a half-extent up to ~1.9 stays fully framed, so
  `--scale 1.8` fills the icon without clipping.
| `--noflipy`     | Keep glTF +Y-up as-is. Default flips Y (PS2 browser shows it upright). |
| `--stretch`     | Stretch the texture to fill 128×128. Default letterboxes it (keeps AR). |
| `--rgba R G B A`| Letterbox margin / no-texture fill color (0–255). Default black. |

Example:

```sh
./glb2icn ../../assets/dc/logo.glb logo.icn
```

## Format notes

- Emits one static animation shape: `header(20) | vertices(N*24) | anim(20+16+8) | texture(128*128*2)`.
- Vertices are a flat triangle list (`N` = a multiple of 3); positions/normals/UVs
  are 16-bit fixed-point (÷4096); vertex color is white so the texture + lighting
  show through.
- Texture is the first material's base-color image packed as BGR555 (uncompressed,
  `tex_type 0x07`). The icon texture is a fixed 128×128, so a non-square source is
  **letterboxed** (fit + centered, margins filled with `--rgba`) to preserve its
  aspect ratio; `--stretch` fills the square instead.
- **The PS2 BIOS rejects large icon meshes as "Corrupted Data."** A ~4.4k-vertex
  logo failed; a 36-vertex cube was fine. So a real model usually can't be an icon
  as-is — convert it with `--flat` (a 12-vertex textured card carrying the image),
  which is what the HyperSolar build uses. Converting the raw mesh errors out past
  `ICN_VERT_MAX` (1800) with a pointer to `--flat`.

## Wiring it into the game

This is already wired in the HyperSolar build:
1. The `Makefile` builds this tool, then runs `glb2icn --cube --tex
   assets/ps2/icon.png build/ps2/logo.icn` and `incbin`s the result
   (`.balign 64` for DMA) as `logo_icn[]`.
2. `ps2_mc_write_browser_icon` (`playstation2.c`) writes `logo_icn[]` as the save
   icon when present, falling back to the runtime procedural cube
   (`ps2_build_cube_icn`) otherwise.
