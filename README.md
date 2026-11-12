# Excel Interactio for OBS — experimental source

This is an **experimental prototype**, not a finished release. It was compiled using the ExcelOverlay OBS 32.2.2 development files and loaded in an isolated OBS 33.0.1 test. Twitch commands, timers, and overlay playback still need end-to-end verification.

## Rules

In OBS, open **Tools → Excel Interactio**. Rules appear as color-coded IF / THEN cards. Use **Add rule**, **Edit**, **Test overlay** or **Delete**; deletion asks for confirmation. The visual rule builder has separate trigger and action blocks with a live preview. Save the main window to apply your changes; Cancel discards them. Example rules:

* `IF command` / `!fire` → `THEN overlay`: choose an existing source and media. The plugin creates a temporary overlay over that source, following its position, rotation, crop and size, for the chosen duration. Cooldown is in seconds.
* `IF command` / `!hello` → `THEN message`: send the text in the content column to Twitch chat.
* `IF timer` / `10` → `THEN message`: the OBS plugin runs the timer and asks ExcelProtocol to send the text every 10 minutes. Timers continue while disconnected, but a disconnected request is skipped and never replayed. Timers run whether or not OBS is streaming; saving settings restarts their countdown.

Use the rule editor's dropdowns for IF, THEN and the source to cover, and its file picker to choose local media. Each source entry identifies its scene. Sources directly inside a scene are supported; items inside groups are not currently listed. The selected source must be visible and its scene displayed for the overlay to appear. Use **Test selected overlay** to preview a saved rule without Twitch. The temporary overlay is removed when it ends.

Choose a built-in Neon frame, Celebration confetti, Twinkling sparks or Digital glitch; select a local media file; or paste a YouTube link. The rule builder links to [Kudos free overlays](https://kudos.tv/collections/freebies), which offers downloadable overlay packs; check each pack's license and supported format. YouTube support is experimental: YouTube can reject direct embeds, embedding may be disabled, and autoplay, audio or ads may prevent the intended result. The overlay stops after the specified duration, not automatically at the end of the clip.

## Updates

The experimental build includes an Updates panel with stable/beta selection and verification code adapted from ExcelProtocol FX. Its official repository is fixed to `StayExcellent666/ExcelInteractio`, but no release has been published. Local tests cover policy validation, checksum verification, and isolated installer behavior; a complete updater run after OBS exits remains required before publishing. Update prompts can request Windows administrator permission when installing.

## ExcelProtocol pairing

Pair the plugin as its own device using a code from the ExcelProtocol dashboard, using the same flow as ExcelProtocol FX. The independent device credential is stored in Windows Credential Manager; Twitch access remains with ExcelProtocol. The connected server must support the companion contract in `EXCELPROTOCOL-INTEGRATION.md`. This prototype has no role restrictions or live-only timer toggle.

## Building

Requires Windows, CMake, MSVC, and prepared OBS development dependencies, including Qt 6 (Widgets and Network), OBS headers, import libraries and a matching TLS backend. Configure CMake with the dependency directory, then build and install:

```
cmake -S . -B build -DEXCEL_OBS_DEPS="path-to-prepared-dependencies"
cmake --build build --config Release
cmake --install build --config Release --prefix "path-to-obs-studio"
```

The existing ExcelOverlay `.deps` directory was used for the isolated build. `cmake --install` installs both the plugin and its TLS backend. The load test did not verify actual Twitch or media interactions.

## Known work remaining

Test the updater end to end before a release; verify pairing, chat commands and timer sends against the deployed ExcelProtocol server; support sources inside groups; properly host and test YouTube embeds; test audio, scene changes, overlapping commands, cleanup, and rate limits. Previous prototype settings may still contain an old plaintext Twitch token; saving settings with this version rewrites the settings without it. Revoke any old token that was exposed.

