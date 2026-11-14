# City scenery: the ruleset for a building

How a building in the True 3D city (City and Peninsula) is made, placed, lit and shaded. Follow these rules when you add facades or signs, or change the shader.

Code:
- `analysis/city_hd/scripts/facades.py` makes the pictures.
- `src/platform/ddraw/world3d.inc` (`W3LoadCityArt`, `W3SunShadowMap`) loads them and sets up the shadows.
- `src/platform/world3d/course3d.h` (`C3_Building`, `C3_FacadeVary`, `C3_BldgStart`) builds the geometry.
- `src/platform/world3d/render3d.h` (`psmain`, `vsshadow` / `psshadow`) does the shading.

## 1. The pictures

| File | Size | What |
|---|---|---|
| `data/mod/widescreen/hd/city/FAC00..63.png` | 512 x 1024 | a whole building front: cornice at the top, floors, shop front at the bottom |
| `data/mod/widescreen/hd/city/SGN00..31.png` | 256 x 768 | a projecting (blade) sign, the word stacked letter by letter |

Every facade starts as a **layout** drawn by `facades.py`, which records the floors, a window per bay, the shop front and the sign band. Fooocus then redraws it as a photograph (img2img, denoise **0.66**: real enough, and the windows stay where the layout put them). Signs use denoise **0.38**, so the lettering survives. After the redraw the lettering is copied back from the layout, so the AI can't misspell it.

Styles (12, cycled over the 36 facades): brick tenement, brownstone, art deco, glass curtain wall, 1970s concrete, stucco, victorian, cast-iron loft, old hotel, tiled mid-century, warehouse, bank. Shop fronts: shop, cafe, bar, lobby, diner, garage, books, deli.

To make more or change them:
```
python facades.py prep work --denoise 0.66     (layouts + jobs.json)
python_embeded\python.exe -s fooocus_img2img.py work\jobs.json
python facades.py post work <mod>\hd\city      (the RGBA files with their codes)
python facades.py sheet work sheet.png         (layout | redrawn, to check)
```

## 2. Texel codes (the alpha byte)

The shader reads what a texel *is* from its alpha byte. Keep the codes exact. Mips blend them at a distance, which is harmless.

| Alpha | Meaning | Shader rule |
|---|---|---|
| 255 | wall | bump mapped from its brightness (darker = lower: mortar, joints, carving); sun-shaded |
| 0xB2 (0.698) | window glass | a **room** behind it (interior mapping), a set-back reveal, reflects the sky, lit at night by the room rule |
| 0x99 (0.600) | shop window glass | a deep **display** (shelves of goods), mostly lit at night |
| 0xA6 (0.651) | sign lettering | glows in its own colour at night |
| 0x8C (0.549) | neon tube | own colour, faint by day, blazing at night |
| 0 | nothing | a sign's outline: cut out |
| 0xFE at texels (0,0) and (1,0) | the facade's **layout** | (0,0) = floors, bays, cornice v x 255; (1,0) = shop line v x 255, shop bays |

A facade without the two layout texels (alpha 0xFE) gets no rooms: its glass is drawn flat. The old course textures use 0.75 = window, 0.65 = sign, and keep their old rules.

## 3. Placement (course3d.h)

- **Buildings are 1 to 6 pieces long.** A new building starts at piece k where `C3_Hu(k, side * 7 + half * 3 + 0.5) < 0.36`. Its facade and height come from its first piece. Height is 0.85 to 1.9 times the course's own. Two neighbours never share a facade.
- Each building is a box `C3_BLDG` (1500) deep: facade, back wall, roof (snow on it in snow), and end walls where the neighbour is missing or a different height.
- **Blade signs:** on about every third piece of a building (`C3_Hu(k * 3 + 1, ...) < 0.30`). Each sign stands square to the facade, 14 to 134 units out over the footpath, with its foot 30 units above the shop line. It is 360 tall, with a dark bracket at the top, and only placed where it fits under the roof line. Both faces are drawn, the back one mirrored, so the word reads from either direction.
- Walls and signs carry vertex alpha **254**. That marks them as "a building" for the shader, which applies the sun shading, the rules above, and lets them cast shadows.

## 4. Windows have depth (render3d.h)

- **Rooms (interior mapping):** the view ray goes into a box behind the glass. The box is one bay wide and one floor high, from the layout texels, and 0.9 bay deep (1.3 for shops). The ray hits the back wall (sometimes with a picture on it), a side wall, the floor (wood or carpet) or the ceiling (with a lamp). Deeper surfaces are darker. Each room has its own wall colour from a hash of its bay and floor.
- **Reveal:** the glass sits 7 units back. Where the shifted view lands outside the pane, you see the wall's side in shade.
- **Shop displays:** the back wall holds shelves of goods: 16 x 5 cells, each a colour of its own.
- **Glass:** 25 % of the redrawn picture (frames, curtains) shows over the room (10 % in shops).

## 5. Reflections

In sunshine all glass reflects the sky, the clouds and the sun's glint, more at glancing angles (Fresnel). For the City set the reflection is scaled down (windows x0.5, shops x0.28) so the rooms still show through.

## 6. Night lighting

- Night strength is `trk.w` (time of day).
- **Windows:** a building is lit if `H(floor(world xz / 900) + 3.1) > 0.25` (three in four). In a lit building, a room is lit when its own hash is below 0.5 (half the rooms). A lit room glows in its own surfaces' colours times warm (1.0, 0.80, 0.52) or, for about 30 %, cool (0.75, 0.85, 1.0) light, x1.25.
- **Shops:** lit when their hash is above 0.12 (nearly all), x1.5.
- **Reveals** never glow.
- **Sign lettering** (0xA6) glows in its own colour. **Neon** (0x8C): its colour normalised and saturated (to the 4th power) x1.8 + 0.1, already at 20 % by day.

## 7. Shadows

- The buildings' shadows come from a **shadow map** (`W3SunShadowMap`, `R3_EnsureShadow`). Only with a real sun, and only with the Esc option BUILDING SHADOWS (or `RR_BLDGSHADOW=1`).
- The depth of the walls (vertex alpha 254, billboards excluded) is drawn from the sun each frame. It is orthographic, 2048 x 2048, over a 10 400-unit box centred 3000 units ahead of the camera. Its texel grid is fixed in the world, so edges don't crawl.
- Every surface that faces the sun (road, footpath, kerb, gutter, ground, other buildings) is darkened where a wall stands between it and the sun. The edge is a soft 3 x 3 sample, and the shadow fades out at the map's edge. Strength is `min(1.6 x sun opacity, 0.55)`.
- Walls facing away from the sun are already shaded by the wall rule. They aren't darkened twice.
- Trees, lamps and poles keep their own projected shadows (`W3CasterShadows`). The old flat building shadows are still available with `RR_W3OLDBSH=1`.

## 8. Test switches

| Switch | Effect |
|---|---|
| `RR_W3NOCITY=1` | the old drawn facades (GENFAC) instead of the City set |
| `RR_BLDGSHADOW=0/1` | building shadows off / on |
| `RR_W3LOGSH=1` | log the sun's shadow setup |
| `RR_W3OLDBSH=1` | the old flat shadows (only when the shadow map is off) |
| `RR_TOD=0..4` | time of day (night: lit rooms, neon) |
