# RealXI

**Offline football practice arena for Android.** Play against the System, a tactical engine written in C++, then let the built-in
coach show you what to fix. No account, no server, no network permission.

## About

RealXI is a free, offline football practice arena built to help players sharpen their game. Instead of just playing matches, you get a
System opponent that adapts to your habits, a set of focused drills, and a coach that shows your recurring weak spots and sends you to the
drill that fixes them. Everything runs on your phone: no account, no ads, no tracking, no internet.

## Download

RealXI is already published. Get the latest APK from **[Releases](../../releases/latest)**, no build required.

1. Download the APK from the latest release.
2. Allow "Install unknown apps" for your browser or file manager, then open the APK.
3. Open RealXI, pick **Match vs System** or **Practice arena**, and play. The app runs in landscape.

## Features

**Match vs System**
- 1 to 5 minute halves, 21 formations (4-4-2, 4-3-3, 4-2-3-1, 3-4-3, 5-2-3 and more), offside, fouls and cards, referee strictness, adjustable aim assist.
- Four System levels: Regular, Hard, Pro and Legend. Levels differ only in perception, decisions and execution. The System never reads your input and gets no stat boosts.
- Optional adaptation: the System learns where you attack, how you finish and how you pass, then tests it.

**Practice arena**
- Eight drills: penalties, free-kick lab, 1v1 vs keeper, 3v2 overload, corner delivery, crossing and finishing, press escape, defend 1v1.
- Each attempt ends with a clear result (goal, saved, missed, lost the ball, escaped, held). Attempts, hit rate, best score, streaks and a mastery level are saved per drill.

**Coach**
- Every match and drill updates a rolling score for passing under pressure, finishing, creating chances, offside timing, tackling discipline, defending and keeping the ball.
- Repeated weak spots appear on the home screen and the full-time panel with a one-tap button to the right drill. The practice arena marks "Coach pick" drills.
- All coach data stays on your device.

**Career**
- Record against each System level, best winning streak, practice streak and drill mastery, all on your device.

**Training tools**
- Slow motion (100/50/25%) with an on-screen speed badge, instant replay of the last 10 seconds with a REPLAY badge and timeline, selectable skill move, System debug overlay (movement targets and defensive line).

## Controls

Buttons change by themselves, like eFootball: green when you have the ball, blue when you do not. In-game help is in **Settings, Controller help**, and a short tip card shows on first launch.

| Slot | With the ball | Without the ball |
|---|---|---|
| Left thumb | Floating stick: move and aim | Same |
| DASH | Hold to sprint, release to tighten control | Sprint to recover or reach the ball |
| PASS / PRESS | Tap for a ground pass, hold for power. Pass assist picks the best team-mate in your aimed direction | Hold to close the carrier down goal-side (jockeys when close) |
| THROUGH / TEAM PRESS | Through ball into space ahead of a runner | Call for pressure: team-mates close the ball down |
| SHOOT / SWITCH | Hold to charge, release to strike | Select the defender closest to the ball |
| LOFT / TACKLE | Lofted pass or cross. In your own third it becomes CLEAR | Tap for a stand tackle, hold to slide |
| SKILL | Sharp touch past a defender (pick the move in the pause menu) | |
| 1-2 | Then PASS: pass-and-run, the passer sprints for the return | |

Button size, opacity, stick dead zone and a large-buttons preset are in Settings. The layout lives in `assets/data/default_control_layout.json`.

## How the System plays

Decisions are scored on one scale: the chance an action becomes a goal, minus the cost of losing the ball.

- **Expected threat (xT):** a zone-value grid rewards passes for the danger they create, weighted by the chance they arrive.
- **Crowd release and triangles:** a pressed carrier releases early to a supported team-mate. Off-ball players offer triangle outlets.
- **Counters and game plan:** quick forward play after winning the ball, and protect-a-lead or chase-a-goal behaviour late in the match.
- **Defending:** pressing, goal-side marking, lane cutting, a shared back line with an offside trap, and a keeper that reads shot paths.

Full detail in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build from source

```
engine/   C++20 engine: physics, rules, System AI (4 levels), drills, replay, C API. Builds on desktop.
app/      Android app: Kotlin + Jetpack Compose UI, 2.5D renderer, multi-touch controls, JNI bridge.
assets/   Formations, drills, control layout, optional sprite sheets and sound effects.
tools/    check_layout.py keeps the Kotlin snapshot layout in sync with the C++ header.
```

Requirements: Android Studio (AGP 8.9), JDK 17, NDK 26, CMake 3.22. Open the folder in Android Studio; it creates the Gradle wrapper on first sync.

Desktop engine tools (g++ only):
```
make engine-test                          # engine regression tests
make sim ARGS="16 3 0 120 7"              # headless self-play: matches, home tier, away tier, half seconds, seed
make check-layout                         # snapshot layout check
```

## Releasing

Tag `v1.0.0` (or any `v*`) and the workflow builds the APK and publishes it to Releases. Signed builds, the Gradle wrapper and Play Store notes are in [docs/RELEASING.md](docs/RELEASING.md).

## License

MIT, see [LICENSE](LICENSE).

## Contributing

Pull requests are welcome. Keep `make engine-test` and `make check-layout` green, add a test for new behaviour, and include numbers that show a tuning change helped.

## Known limitations

- Player figures use generated pixel sprite sheets; replace them with your own art by dropping `sheet_home.png` and `sheet_away.png` into `assets/sprites/players/` (see its README).
- System-vs-System matches are low scoring; tempo against a human is still being tuned from play-testing feedback.

*Built by Reinford, Kenya. Independent project.*
