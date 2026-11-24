# Player sprite sheets

`sheet_home.png` (green/gold kit) and `sheet_away.png` (white/blue kit) are loaded by `SpriteAtlas` in `PitchRenderer.kt`.

* One row of 6 equally wide frames, 32x48 px each, all facing **right** (the renderer mirrors them): idle, run A, run B, kick, slide, dive/fallen.
* Feet sit on the bottom row of each frame (the renderer anchors the bottom edge to the player's position).
* Any frame size works (height is taken from the image, width = image width / 6). Nearest-neighbour scaling is used, so keep the art pixel-crisp.
* Goalkeepers use the sheet of their team (they are tinted by the placeholder renderer only when no sheet exists).
* Delete a sheet to fall back to the built-in procedural figure for that team.
