# Roadmap

Feature targets come from [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp)'s feature list.
The app layer follows [BanjoRecomp](https://github.com/BanjoRecomp/BanjoRecomp), which uses the shared
[RecompFrontend](https://github.com/N64Recomp/RecompFrontend) for the launcher, menus, input and mod UI.
Zelda's own frontend predates RecompFrontend and is mostly game-specific.

## Phase 1: Boot and basics
- [x] Recompile all main-segment CPU code (1697 functions)
- [x] Identify the RSP microcodes: graphics F3DEX.NoN 1.21 (RT64 HLE), audio n_aspMain (RSPRecomp)
- [x] App glue: RT64 renderer, SDL audio, ultramodern/librecomp, RecompFrontend launcher + config tabs
- [x] Boot through intros to the save-select and difficulty menus
- [x] Keyboard/controller input reaches the game (default bindings; menus navigate)
- [x] Audio playback (n_aspMain via RSPRecomp, 22 kHz stereo)
- [x] EEPROM detected (save-select screen loads)

## Phase 2: Playable
- [x] Reach gameplay in the first mission (Ice Hit); movement works
- [ ] Play through the first mission; fix crashes, rendering and audio issues as they appear
- [ ] Name the game's main subsystems (scheduler, main loop, input, audio, renderer) in `symbols/manual.txt`
- [ ] Rumble Pak / Controller Pak support
- [ ] Remove waits on RCP hardware that the runtime doesn't need (as Zelda's `[[patches.instruction]]` do)

## Phase 3: Enhancements (Zelda64Recomp parity)
- [x] 60 FPS: the level loop's limiter (min 2 VIs/frame) is configurable; game logic runs on the VI clock so
      speed is unchanged (verified by comparing 30/60 FPS runs). Graphics > Frame Rate, default 60.
- [ ] Widescreen / ultrawide (RT64 aspect ratio plus patches for HUD and culling)
- [ ] Dual analog camera
- [ ] Gyro / mouse aim (the game has first-person aiming)
- [ ] Low input lag (present-early mode is already on)
- [ ] Instant load times (ROM DMA is already instant in the runtime)
- [ ] Linux packaging (Flatpak/AppImage) and Windows builds

## Phase 4: Mods
- [ ] Patches pipeline: `patches/` C compiled for MIPS, recompiled with `patches.toml` (like Banjo's PatchesLib)
- [ ] Export the symbol file for mod authors (`RecompModTool`, a `.nrm` template)
- [ ] Texture packs (`.rtz`, RT64 replacement)
- [ ] Mod config menu integration
