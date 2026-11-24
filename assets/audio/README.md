# Audio

`sfx/` holds short synthesized sound effects (44.1 kHz mono, Ogg Vorbis, generated procedurally, no third-party samples):

| File | Used for |
|---|---|
| `kick.ogg` | passes, shots, headers |
| `whistle.ogg` | kick-off, half/full time, fouls, offside |
| `goal.ogg` | goals and successful drills (crowd swell) |
| `post.ogg` | woodwork |
| `save.ogg` | keeper saves and catches |
| `tackle.ogg` | tackles |

Replace any file with your own `.ogg` of the same name to change a sound; a missing file is skipped silently (see `SoundFx.kt`).
