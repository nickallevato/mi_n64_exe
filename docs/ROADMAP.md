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
- [x] All 20 levels load and run (scripts/level_test.sh: unlock, navigate, 20 s random play): 59-61 FPS, no
      crashes or stalls; Waterloo Station's full intro cutscene and sniper section play correctly
- [ ] Full playthroughs of each mission (objectives, scripted events, endings)
- [x] Music plays through the intro, menus and levels (confirmed by playtest; spectrograms of menu and level audio)
- [x] Clean shutdown: quitting crashed 4/5 times (game threads ran on after game memory was unmapped); now 0/11 across
      intro, title and in-level quits
- [ ] Name the game's main subsystems (scheduler, main loop, input, audio, renderer) in `symbols/manual.txt`
- [ ] Rumble Pak / Controller Pak support
- [ ] Remove waits on RCP hardware that the runtime doesn't need (as Zelda's `[[patches.instruction]]` do)

## Phase 3: Enhancements (Zelda64Recomp parity)
- [x] 60 FPS: the level loop's limiter (min 2 VIs/frame) is configurable; game logic runs on the VI clock so
      speed is unchanged (verified by comparing 30/60 FPS runs). Graphics > Frame Rate, default 60.
- [x] Upgraded graphics: RT64 renders at window resolution with MSAA and widescreen "Expand" by default
      (compared against native 240p 4:3 at the same moment). HUD stays 16:9-clamped.
- [ ] Ultrawide polish (culling at extreme aspect ratios not checked)
- [x] Analog camera: right stick turns Ethan (and the camera behind him) at a configurable rate and tilts the
      first-person view; right stick removed from the C-button defaults. General > Analog Camera (default on)
- [x] Keyboard + mouse: WASD move, mouse turns Ethan directly (writes his yaw), right mouse = R (first-person view,
      mouse moves the crosshair), left mouse = Z (shoot), Space = A, Shift = B, Ctrl = crouch. Cursor is captured in
      game; General > Mouse Sensitivity (default 50%).
- [x] Visual controls map (docs/controls.svg, in the README); camera mode moved to V (E was on both L and R)
- [x] Mouse/right stick in sniper mode (Waterloo Station, Mainland): added as stick deflection in
      Camera_SniperControl, verified panning in all directions in Waterloo Station
- [x] Mouse aims the Gunboat turret (normal path; A/B-compared against a no-input run)
- [ ] Player_AimControl (state 0x100000) mouse hooks: that state is only entered by a script command
      (func_8003D844); not reached in any tested level
- [ ] Low input lag (present-early mode is already on)
- [ ] Instant load times (ROM DMA is already instant in the runtime)
- [x] Runs well on Linux/GNOME/NVIDIA: native Wayland, mailbox presentation (no freeze when hidden), <1 s
      startup, ~22% CPU, 3-minute random-input soak without issues, identical audio at 30/60 FPS
- [ ] Linux packaging (Flatpak/AppImage) and Windows builds

## Phase 4: Mods
- [ ] Patches pipeline: `patches/` C compiled for MIPS, recompiled with `patches.toml` (like Banjo's PatchesLib)
- [ ] Export the symbol file for mod authors (`RecompModTool`, a `.nrm` template)
- [ ] Texture packs (`.rtz`, RT64 replacement)
- [ ] Mod config menu integration
