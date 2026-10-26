# OC2 · 我的工作（精简参考仓）

**Private** · 只包含 `ellingtoncody81-ctrl` 自己的提交与改动，**上游历史已剥离**。

上游三个 GoldSrc 项目的完整历史本身就是公开的，留在本仓没有意义，只会把体积撑到 100 MB 以上。
本仓把每个仓重写成一条**只含自有提交**的链，且每棵目录树**只保留你实际改过的文件**。

## 分支

| 分支 | 源分支 | 自有提交 | 树内文件 |
|---|---|---:|---:|
| `cs16-client` | `oc2-game-optimization` | 46 | 0 |
| `xash3d-fwgs` | `oc2-game-optimization` | 12 | 0 |
| `regamedll` | `oc2-game-optimization` | 12 | 0 |
| `mainui` | `oc2-game-optimization` | 10 | 0 |
| `cssdk` | `oc2-game-optimization` | 2 | 2 |

## 提交对照（原提交 → 精简提交）

每个仓的提交按原顺序一一对应，作者、日期、提交信息原样保留。

<details><summary><b>cs16-client</b>（46 条）</summary>

| 原提交 | 精简提交 | 日期 | 说明 |
|---|---|---|---|
| `7bc5cd02` | `19fb6a62` | 2026-10-11 | chore(oc2): pin regamedll fix, ignore build_regamedll |
| `d03f08a8` | `1e271092` | 2026-10-11 | feat(radar): add CHudRadarModern (CS:GO radar) reconstructed from the apk |
| `e6741163` | `e132fc2f` | 2026-10-11 | radar: anchor overview on the player, unify blip transform |
| `3890e983` | `2999d644` | 2026-10-11 | radar: fix anchor double-count, bind white for the disc, clip tiles to the circle |
| `1929d464` | `f5141c5f` | 2026-10-11 | radar: turn the map with the view, navy plate, self marker |
| `5f13e2ea` | `4373736b` | 2026-10-11 | radar: the overview origin is the image's centre-x / top edge, not its corner |
| `25e420bd` | `2351a445` | 2026-10-11 | radar: rewrite the mapping in tile space, per the finished reverse engineering |
| `43509950` | `4f938f84` | 2026-10-11 | radar: fix the tile mapping - axes are not crossed, X sign was flipped |
| `6355f36e` | `e1498a36` | 2026-10-11 | radar: expose the residual tile offset, make the centre marker unconditional |
| `52ebdca2` | `c2209cde` | 2026-10-11 | radar: derive the world->overview mapping from the map bounds |
| `4706f5af` | `684a6330` | 2026-10-11 | radar: point the self arrow the way the player faces on the rotated grid |
| `c8ac62b5` | `aa950a3b` | 2026-10-11 | radar: blip origins, radar zoom and the invisible self arrow |
| `485936c1` | `4ebfb3d7` | 2026-10-11 | radar: derive the self arrow from the grid basis |
| `5f7d3454` | `7258d0a1` | 2026-10-11 | radar: the tile column takes world y, the tile row takes world x |
| `dbd6aaab` | `d8f199a9` | 2026-10-12 | radar: rotate the map by yaw+90, feed the self arrow tile-space facing |
| `252d653b` | `7755e8bf` | 2026-10-12 | radar: drop blips outside the circle, teammates blue and enemies yellow |
| `0bdbc74e` | `fa379779` | 2026-10-12 | radar: take the blip team from the entity state, enemies red |
| `b43cdc11` | `4e33fb7d` | 2026-10-12 | radar: get the team over the one channel that actually reaches the client |
| `e8a35dea` | `b767b6ed` | 2026-10-12 | radar: fix ROTATED overview mapping and derive the self arrow from it |
| `724bf2cc` | `9f517cb2` | 2026-10-13 | radar: gate enemy blips behind a real line-of-sight test |
| `7c39b109` | `72eacd7f` | 2026-10-13 | radar: acceptance build - circular frame and blurred backdrop |
| `335ac61f` | `3261c5b9` | 2026-10-13 | hud: CS:GO view bob behind cl_bobstyle 2 |
| `ae97463c` | `ceb61fc2` | 2026-10-13 | viewmodel: CS:GO view bob instead of the GoldClient fork |
| `6223daf3` | `4227ddf9` | 2026-10-13 | viewmodel: stop the bob stuttering on ramps and stairs |
| `5d1147d3` | `f40e518f` | 2026-10-13 | viewmodel: CS:GO weapon sway, and stop the lateral bob vanishing on pistols |
| `e4d55f23` | `d7a3219b` | 2026-10-14 | fix(hud): restore viewmodel effects and smooth downhill camera |
| `f38d9c30` | `c9cf17cd` | 2026-10-14 | fix(input): honor fast switch for wheel weapon selection |
| `67bae185` | `cf0e126f` | 2026-10-14 | docs: keep project notes in the canonical md directory |
| `e155f107` | `d242398d` | 2026-10-15 | feat(ui): integrate avatars, match HUD and purchase menu |
| `b8335f51` | `381778b3` | 2026-10-16 | feat(equipment): complete weapons, sights and HUD feedback |
| `20726a48` | `c110f6de` | 2026-10-18 | Complete module 3 HUD, aiming and weapon flow fixes |
| `08128beb` | `ee95172d` | 2026-10-19 | Finish model ADS polish and Source-style viewmodel controls |
| `56e4e9ea` | `ef105a17` | 2026-10-19 | Stabilize viewmodel motion, reload playback and muzzle effects |
| `22e4db38` | `65f20c63` | 2026-10-21 | feat(inventory): attach counters and sticker charms to weapon surfaces |
| `107065f2` | `7555b210` | 2026-10-21 | fix(inventory): refresh saved accessory placement without stale anchors |
| `ea8c44d5` | `aad6a828` | 2026-10-23 | fix(attachments): preserve mounts and constrain charm swing |
| `9924526c` | `d5dca800` | 2026-10-24 | feat(inventory): render persistent weapon and knife cosmetics |
| `cab9d379` | `3ddc0bad` | 2026-10-24 | feat(inventory): complete cosmetics and multiple inspect animations |
| `cf8696f3` | `90624e69` | 2026-10-26 | feat(client): improve combat effects and continuous body animation |
| `27fd257f` | `3255dbde` | 2026-10-26 | fix(animation): update layered first-person body with current view |
| `d6d31c97` | `01640f5f` | 2026-10-26 | fix(client): keep first-person body intact through jumping |
| `3840fd81` | `164885ec` | 2026-10-26 | fix(client): align body framing and Source weapon motion |
| `d93c9781` | `0b87b1f4` | 2026-10-26 | fix(client): preserve body placement and natural ragdoll falls |
| `62e41539` | `c6eb2bc8` | 2026-10-26 | fix(client): stabilize jump torso and predicted crouch body |
| `1687b1d4` | `53b8e4c9` | 2026-10-26 | fix(client): stabilize reload and body poses with world gloves |
| `768f282f` | `f61cfccb` | 2026-10-27 | fix(client): unify actors and improve death and HUD presentation |

</details>

<details><summary><b>xash3d-fwgs</b>（12 条）</summary>

| 原提交 | 精简提交 | 日期 | 说明 |
|---|---|---|---|
| `d1ae2d10` | `672055c8` | 2026-10-11 | feat(radar): engine-side CS:GO radar background blur API |
| `086af360` | `c389b48a` | 2026-10-11 | radar: export the BSP world-model bounds to the client |
| `7f9a50d4` | `05e2aeea` | 2026-10-12 | engine: let a windowed listen server actually honour maxplayers and '+' commands |
| `cdd3008e` | `6c1713c5` | 2026-10-12 | radar: get the team over the one channel that actually reaches the client |
| `dc96a658` | `2330c293` | 2026-10-13 | radar: capture the blur backdrop before the HUD is composited |
| `5b539e6b` | `8c991d13` | 2026-10-15 | fix(ui): expose session identity and isolate preview mirroring |
| `671ce415` | `c1a1633e` | 2026-10-16 | fix(render): unify viewmodel handedness and preserve menu wheel input |
| `27fc012d` | `f210dc2f` | 2026-10-19 | Support independent viewmodel FOV projection |
| `a6d91b73` | `36e412e4` | 2026-10-24 | feat(ui): isolate native model preview rendering |
| `a541f377` | `ebada6d8` | 2026-10-26 | fix(render): composite flash blindness after all interface layers |
| `9cabbba5` | `92696572` | 2026-10-26 | feat(console): add a movable native console window |
| `d59b7b23` | `bc101de0` | 2026-10-27 | feat(ui): adopt Source-style console and loading presentation |

</details>

<details><summary><b>regamedll</b>（12 条）</summary>

| 原提交 | 精简提交 | 日期 | 说明 |
|---|---|---|---|
| `fb788629` | `678bbab1` | 2026-10-11 | fix(server): disable file consistency check for offline play |
| `732bcdbf` | `c60f8ee4` | 2026-10-12 | radar: get the team over the one channel that actually reaches the client |
| `11788f75` | `c6c94f88` | 2026-10-15 | feat(hud): synchronize round resets and purchase eligibility |
| `b5ae1a60` | `d68b5707` | 2026-10-16 | feat(equipment): complete modern purchases and weapon presentation |
| `df514a61` | `3821fcab` | 2026-10-18 | Complete module 3 weapon parameters and reload timing |
| `dae5668c` | `847268d5` | 2026-10-19 | Fix weapon aim speed, reload feedback and round ammo stock |
| `77c20813` | `68722673` | 2026-10-19 | Fix purchase animation triggers and per-round Glock feedback |
| `7fb99ea3` | `7ac53fe6` | 2026-10-24 | feat(inventory): match equipped knife animation durations |
| `b7f735b1` | `88a0509d` | 2026-10-24 | feat(inventory): sync bot cosmetics and player gloves |
| `8978885a` | `144f6526` | 2026-10-26 | fix(weapons): correct delayed casing origin and facing |
| `41f8be21` | `58ff6a5f` | 2026-10-26 | feat(inventory): assign and replicate bot glove loadouts |
| `2421bb48` | `9b8dcc0d` | 2026-10-27 | fix(player): use lobby-matched combat models for both teams |

</details>

<details><summary><b>mainui</b>（10 条）</summary>

| 原提交 | 精简提交 | 日期 | 说明 |
|---|---|---|---|
| `85f2d3ca` | `70148cf4` | 2026-10-15 | feat(ui): add five-column buy menu and player previews |
| `431e420b` | `d7025c89` | 2026-10-16 | feat(buy-menu): add continuous scrolling and modern equipment cards |
| `6105c923` | `8a0b5294` | 2026-10-18 | Refine OC2 buy menu scrolling and character proportions |
| `d666fb65` | `f5b39189` | 2026-10-21 | feat(inventory): add modern frontend and weapon accessory editor |
| `931b29b1` | `a170c100` | 2026-10-21 | fix(inventory): activate edited weapon accessories when saving presets |
| `bdfbb873` | `d537df22` | 2026-10-23 | fix(inventory): preview attachments at their actual size |
| `7f28f41d` | `daa7ca10` | 2026-10-24 | feat(inventory): equip weapon finishes knives and sticker slots |
| `24db0f87` | `d1d8fda4` | 2026-10-24 | feat(ui): add panorama layouts and 3D inventory previews |
| `8e2e0728` | `1069e989` | 2026-10-26 | feat(ui): refine Chinese inventory and local match presentation |
| `cc599cb7` | `91a8b471` | 2026-10-27 | feat(menu): show map loading and complete Unicode font glyphs |

</details>

<details><summary><b>cssdk</b>（2 条）</summary>

| 原提交 | 精简提交 | 日期 | 说明 |
|---|---|---|---|
| `248cc8d2` | `d592a565` | 2026-10-16 | feat(equipment): extend shared weapon and inventory definitions |
| `e98a85c4` | `b888ab3b` | 2026-10-18 | Align OC2 weapon limits with CS:GO equipment |

</details>

## 与上游的关系

本仓的提交链**不含上游历史**，所以：

- 它是**参考/审阅**用的，不是可直接编译的工作树
- 要把改动应用回上游，用原仓的提交号（对照表左列）：

```powershell
git -C <上游仓> cherry-pick <原提交号>
```

- 上游仓库地址：
  - `cs16-client` → <https://github.com/Velaron/cs16-client>
  - `xash3d-fwgs` → <https://github.com/FWGS/xash3d-fwgs>
  - `regamedll` → <https://github.com/Velaron/ReGameDLL_CS>
  - `mainui` → <https://github.com/Velaron/mainui_cpp>
  - `cssdk` → <https://github.com/s1lentq/CSSDK>

## 精简方式

1. 提交链：只保留作者为 `ellingtoncody81-ctrl <ellingtoncody81@gmail.com>` 的提交，按原拓扑顺序重写为链式
2. 目录树：每个提交的树只保留**你自己改动过的路径**（逐提交累积），上游原封不动的文件全部剔除
3. 体积：原 `oc2-reference`（含上游全历史）112.4 MB → 精简版约 1.5 MB

## 未包含

- 上游第三方项目的代码与历史（公开可取，见上）
- `D:\xashcs2` 下的 MD 文档、模型、测试证据 —— 本就不在任何 Git 仓内
- 工作区未提交的在改内容（精简仓基于已提交状态构建）

## 版权

`cl_dll/` 等路径下的第三方 GoldSrc 源码版权归各自作者所有，许可证遵循各上游仓库原状。
