# Dingoo A320 / Gemei X760+ 3D Sample Baselines

This document records repeatable startup, timing, and visual smoke baselines for
the local Dingoo A320 handheld and Gemei X760+ handheld `.app` sample set. Game
files are not stored in this repository. The `.app` container format belongs to
Dingoo Technology; samples must be legally obtained by the user.

## Test Command

```powershell
.\scripts\profile_samples.ps1 `
  -SampleDir '<local Dingoo A320 or Gemei X760+ sample directory>' `
  -BuildDir 'D:\Project\C++\dingoo-pie\build\win64' `
  -OutputDir 'D:\Project\C++\dingoo-pie\test_artifacts\a320-x760plus-3d-baseline2' `
  -Seconds 10 `
  -SkipProfileSamples 2 `
  -DumpFrameStart 120 `
  -DumpFrameEnd 360 `
  -DumpFrameStep 120 `
  -Backend both
```

Artifacts from the run:

- CSV: `D:\Project\C++\dingoo-pie\test_artifacts\a320-x760plus-3d-baseline2\20260614-135403-summary.csv`
- JSON: `D:\Project\C++\dingoo-pie\test_artifacts\a320-x760plus-3d-baseline2\20260614-135403-summary.json`
- Markdown: `D:\Project\C++\dingoo-pie\test_artifacts\a320-x760plus-3d-baseline2\20260614-135403-summary.md`

## Result Summary

All 14 samples started in both backends and the harness found no remaining
`DingooPie` or `dingoo-pie` process after the batch run. The JIT backend is the
playability baseline for this set. The interpreter backend is retained as a
diagnostic fallback; most 3D samples are too slow there during the first
10-second startup window.

| Sample ID | Original sample | SHA256 | JIT FPS | JIT Status | Interpreter FPS | Interpreter Status | Notes |
|---|---|---|---:|---|---:|---|---|
| sample01 | Alibaba | `1B5A929A93DDA5C312E01205F95F363EFA0F69F1EAD2F703714D4366F8495912` | 45.29 | Starts, low content change | 4.86 | Starts, too slow | Needs manual menu input baseline. |
| sample02 | Block Breaker | `C5ADC7DED226705FCB3A1AA80AC41D9AB96B6B6916D99A59A7068FEA722B9F93` | 57.00 | Stable menu after fix | 9.50 | Starts, too slow | Fixed by enabling the existing Block Breaker `.bin` resource view for this hash. |
| sample03 | Dicke Snake | `22531CCED426F19232613C8235B44A3DD4CDECDA18CD6A517044DC05160C5D39` | 58.86 | Stable | 58.71 | Stable | Use as the 2D/JIT/interpreter regression guard. |
| sample04 | Lianliankan | `59DD65FE27D82293B828570C4F3D34874EA265E518F0DC150B58D21489C0A722` | 36.57 | Starts, static menu | 3.57 | Starts, too slow | Needs menu input baseline. |
| sample05 | Dou Dizhu | `A591E374807627B8E8A952F5421349AFDEF9FC99F4DC0B982418A1C0323C6A89` | 38.29 | Starts, static menu | 3.57 | Starts, too slow | Needs menu input baseline. |
| sample06 | Tetris | `78D190E4ABFCDC2C4134DBD185B980BB6F71F137481951503F1533912C9F05EB` | 57.57 | Starts, static menu | 6.57 | Starts, too slow | JIT is suitable for smoke checks. |
| sample07 | Ultimate Drift | `E4E23B19515716445EEE4A79BF6F081B77F5C0911D43456205902475653373F9` | 58.43 | Stable menu | 23.14 | Starts, marginal | JIT frame shows the sound toggle menu. |
| sample08 | Rubi Rubi | `2804FF20F07F82BDCA59EB1BCD6ACE9615862788559F865E11BF0F67547BE6F1` | 34.71 | Starts, static menu | 2.43 | Starts, too slow | Needs menu input baseline. |
| sample09 | PoPo Bash | `387DE314AC5A96A00FF4E85AAACCE14265305270ACD1C1DF6004F59976D0D57B` | 31.33 | Starts | 1.71 | Starts, too slow | Existing quit promotion covers this hash. |
| sample10 | 7Days | `AF681C338A9932C98A3B450D4391C43D13747F1DFD937232AE38BEDB44359BF0` | 7.71 | Slow loading/title path | 0.00 | Too slow | Existing `7Days` delay profile applies. Longer JIT run is needed for manual playability. |
| sample11 | Candy House | `A374186A06EDF34B1BEA824679AEA087393D2BC441BE963C22A057D7B82A9978` | 27.29 | Starts, visible content changes | 0.43 | Too slow | JIT is the only practical baseline. |
| sample12 | Tiandi Dao | `6FA335AD49FE2FE68E6ECE552D72C2DEC352E715B7255FDCE9AED88248FB2C23` | 8.29 | Slow 3D loading path | 0.14 | Too slow | Logs show S3D/BSP loading; first 10 seconds may not reach gameplay. |
| sample13 | War God Xingtian | `71C10376DEDEEB30607D9C332F883FF549962094311A967618C9C323A2C18331` | 16.00 | Starts, slow | 1.57 | Too slow | Needs longer JIT run and input route. |
| sample14 | Zhao Yun Zhuan | `3A59BD1C0DABFF74C8CCED69F50E3E95BC74CE0EA613AD6BE9D77F48D9967ECE` | 32.86 | Starts, static menu | 2.00 | Starts, too slow | Needs menu input baseline. |

## Manual Verification Checklist

For each sample, use the artifact directory named in the batch Markdown summary.

1. Confirm the dumped frame is not black, all-black-with-a-logo-only, or visibly corrupted unless the log proves the game is still loading.
2. In JIT mode, navigate the title/menu for at least one input action and record the successful `DINGOO_PIE_AUTOPRESS_SEQUENCE`.
3. If the game exposes an in-game quit option, record a quit sequence and verify `Get-Process DingooPie,dingoo-pie -ErrorAction SilentlyContinue` returns no process.
4. For interpreter mode, mark a sample playable only when it reaches visible interaction above roughly 20 FPS. Otherwise keep it as a compatibility diagnostic.
5. When a run fails, preserve the `.debug.log`, `.profile.json`, and last frame dump before changing emulator code.

## Exit Baseline Workflow

Use `scripts\quit_samples.ps1` for in-game quit validation. This is separate
from the profile baseline because the profile harness intentionally force-stops
the emulator after a fixed duration. A valid game-quit baseline must show
`natural_exit=true`; a run with `stopped_by_harness=true` is cleanup only.
If a sample returns to its title screen after the quit option, record that as
`return_to_title` and continue with the next exit step from the title screen.

```powershell
.\scripts\quit_samples.ps1 `
  -SampleDir '<local Dingoo A320 or Gemei X760+ sample directory>' `
  -BuildDir 'D:\Project\C++\dingoo-pie\build\win64' `
  -OutputDir 'D:\Project\C++\dingoo-pie\test_artifacts\a320-x760plus-3d-quits' `
  -TimeoutSeconds 20 `
  -Backend ppsspp_irjit `
  -DefaultAutoPressSequence 'START@1500:150,MENU@3500:150,A@5000:150'
```

When a game's final quit action logs `hle: task stop ... promoted=0` and the
process stays alive, add only the observed SHA256 + return-address pair to
`compat_profile.cpp`. Do not promote all subtask `OSTaskDel` calls, because
several games use short-lived worker tasks during normal loading and gameplay.

- `Alibaba` (`1B5A929A93DDA5C312E01205F95F363EFA0F69F1EAD2F703714D4366F8495912`): `START` enters the game. The observed quit flow from the exit option returns to the title screen (`return_to_title=true`) instead of closing the process.

## Known Follow-Ups

- `sample02` / Block Breaker: fixed by adding the `C5ADC7DED226705FCB3A1AA80AC41D9AB96B6B6916D99A59A7068FEA722B9F93` hash to the existing Block Breaker compatibility profile. The log should contain `fsys: applied Block Breaker .bin resource view name=brick.bin`; the previous `malloc(0x260a1300)` failure should not appear.
- `sample12` / Tiandi Dao and `sample10` / 7Days: the first 10 seconds are dominated by 3D engine or title loading. Longer JIT runs plus menu input baselines are needed before judging gameplay.
- Chinese paths work for startup, but PowerShell 5 child-process log output can mojibake non-ASCII paths. Use hashes and artifact directories as stable identifiers.
