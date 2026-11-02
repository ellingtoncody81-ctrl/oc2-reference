# OC2 · 我的工作（精简参考仓）

**Private** · 只包含 `ellingtoncody81-ctrl` 自己的提交与改动，**上游历史已剥离**。

上游三个 GoldSrc 项目的完整历史本身就是公开的，留在本仓没有意义，只会把体积撑到 100 MB 以上。
本仓把每个仓重写成一条**只含自有提交**的链，且每棵目录树**只保留你实际改过的文件**。

## 分支

| 分支 | 源分支 | 自有提交 | 树内文件 |
|---|---|---:|---:|
| `cs16-client` | `oc2-game-optimization` | 123 | 0 |
| `xash3d-fwgs` | `oc2-game-optimization` | 24 | 0 |
| `regamedll` | `oc2-game-optimization` | 41 | 0 |
| `mainui` | `oc2-game-optimization` | 35 | 0 |
| `cssdk` | `oc2-game-optimization` | 2 | 2 |

## 提交对照（原提交 → 精简提交）

每个仓的提交按原顺序一一对应，作者、日期、提交信息原样保留。

<details><summary><b>cs16-client</b>（123 条）</summary>

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
| `702265f7` | `44e4dfbf` | 2026-10-27 | fix(client): improve shell ejection and animation transitions |
| `0d82e077` | `99bf9766` | 2026-10-27 | fix(hud): fit bomb icons and raise animated notices |
| `551cb5dc` | `4a37d553` | 2026-10-27 | fix(inventory): update menu submodule for sticker ordering |
| `381506fe` | `ddcd423c` | 2026-10-28 | fix(inventory): update menu pointer for preview refinements |
| `4b5834e8` | `1fe8c76a` | 2026-10-28 | fix(viewmodel): smooth aiming motion and interrupted transitions |
| `de26abd8` | `0099b6d3` | 2026-10-28 | fix(hud): restore radio menu and equipment presentation |
| `44f9a35f` | `1ef4f7c3` | 2026-10-28 | feat: display carried weapons and equipment |
| `bc8c88ff` | `3dbc234a` | 2026-10-29 | perf(inventory): stabilize carried skins and world sticker rendering |
| `f87f9146` | `8a750f49` | 2026-10-29 | chore(server): advance pickup and dropped-item physics |
| `2533a74b` | `e4881d2d` | 2026-10-29 | chore(server): advance grounded weapon rest alignment |
| `19e06393` | `62123fb8` | 2026-10-29 | chore(server): advance grounded weapon rest gates |
| `7a916096` | `3ba37e26` | 2026-10-29 | feat(radar): show bomb sites, hostages and the C4 carrier correctly |
| `274c9c48` | `c89be57f` | 2026-10-30 | feat(radar): enemy ghosts, death X and sight windows |
| `23866f31` | `de0ba918` | 2026-10-30 | fix(radar): restore teammate and enemy blips |
| `d4255fe6` | `8f6018b5` | 2026-10-30 | feat(radar): real death X, C4 carrier and dropped-bomb markers |
| `ef121601` | `8eaca68e` | 2026-10-30 | style(radar): shrink C4 markers to blip size |
| `ab506692` | `7b2e33e1` | 2026-10-30 | fix(radar): 修复标记偏移、阵营配色与 C4 图标显示 |
| `0f2e75c3` | `edb18740` | 2026-10-30 | fix(radar): 掉落 C4 改用箱子实体定位，去掉移动丢弃的闪现与 1 秒空窗 |
| `9a294e68` | `ce50b212` | 2026-10-30 | fix(radar): 掉落C4按消息定位、队伍判定统一，修白边/红环/包点顺序 |
| `6fa5c3c2` | `9e4a1fa8` | 2026-10-30 | fix(radar): preserve lifecycle state and reject stale bomb positions |
| `5864e37f` | `47af06a2` | 2026-10-30 | feat(hostage): render carried hostages and synchronize radar states |
| `5eac2851` | `0d2ac8fd` | 2026-10-31 | fix(hostage): anchor rescue labels and show rescue equipment |
| `66dff788` | `ed16ae10` | 2026-10-31 | restore wood and grass footstep materials |
| `15c3a047` | `97695f33` | 2026-10-31 | fix(radar): display filtered hostage states for T |
| `02117dfa` | `5fa9742e` | 2026-10-31 | feat(hud): redesign spectator UI and full-map radar |
| `a1fdad90` | `d9b2e64c` | 2026-11-01 | fix(spectator): recognize dropped C4 model |
| `faa4442f` | `7f5f6e96` | 2026-11-01 | chore(submodule): advance purchase sound build |
| `57b8ea5f` | `24367991` | 2026-11-01 | fix(buymenu): re-enable the Zeus once the held one is spent |
| `336481a5` | `457083c4` | 2026-11-01 | chore(submodule): advance inventory wheel and dual-pistol preview |
| `938dc76c` | `04df97b8` | 2026-11-01 | chore(submodule): advance wheel nudge and dual-pane rotation |
| `c4d61fe0` | `60460e00` | 2026-11-01 | chore(submodule): advance rigid dual-pane preview |
| `cd6241e3` | `64cb1070` | 2026-11-01 | chore(submodule): advance one-view Elite preview and wheel cache |
| `e73e0165` | `8a5d6fe6` | 2026-11-01 | chore(submodule): advance dual-axis alignment |
| `eafddccd` | `f4dedb06` | 2026-11-01 | chore(submodule): scope dual layout to inspect |
| `fda8b121` | `57683a42` | 2026-11-01 | chore(submodule): advance frontend entry defaults |
| `5ff130ca` | `a84bb0f8` | 2026-11-01 | chore(submodule): advance M249 wheel nudge |
| `094d8137` | `7dec0eb5` | 2026-11-01 | fix(spectator): refine observer layout and equipment icons |
| `7cea5ad8` | `e1a62b30` | 2026-11-01 | chore(server): pin verified native use pickup |
| `8d878619` | `5019048f` | 2026-11-01 | feat(client): adapt observer controls and bot appearance transfer |
| `59b33653` | `bc73be37` | 2026-11-01 | feat(hud): add AI portraits and refine combat feedback |
| `e743d4eb` | `4ac4d1d8` | 2026-11-02 | perf(client): stream cosmetics and finish combat presentation |
| `64d2d01d` | `6fa211e8` | 2026-11-02 | fix(client): smooth match transitions and bound stage notices |
| `c1cddcb9` | `aedd0d11` | 2026-11-02 | fix(client): unify center feedback and retain M4 charm colliders |
| `9cc456b8` | `1f1ff452` | 2026-11-02 | perf(client): bound charm work and add individual bot profiles |
| `011a74d6` | `fa8cdfd3` | 2026-11-03 | fix(client): validate radar and corpse snapshots and cache appearances |
| `921adbfe` | `83d79c11` | 2026-11-03 | fix(hud): unify spectator notices and clarify observer information |
| `c876a028` | `b2a3a510` | 2026-11-03 | fix(menu): update charm selection UI |
| `70f64f00` | `de5158af` | 2026-11-03 | fix(menu): update charm description font |
| `48ee3660` | `6cf7e856` | 2026-11-03 | fix(appearance): calibrate side mounts and map saved positions to world guns |
| `99ca8e9c` | `0b89c339` | 2026-11-03 | feat(accessories): persist editable mount defaults across views |
| `b41b21e7` | `877a90ea` | 2026-11-03 | fix(inventory): preserve pending charm image selections |
| `987ee3f6` | `9080ead9` | 2026-11-03 | fix(inventory): update static mount editor submodule |
| `4ed26206` | `eed1dc5b` | 2026-11-03 | fix(inventory): update M3 preview alignment submodule |
| `17396641` | `582b6e5c` | 2026-11-03 | fix(inventory): make default weapon and charm artwork transparent |
| `97b09684` | `952af155` | 2026-11-03 | fix(inventory): update charm parameter spacing submodule |
| `1db9308e` | `54b57ad8` | 2026-11-03 | fix(game): update dual pistol presentation and match lobby submodules |
| `5bcdce2e` | `4dc47623` | 2026-11-03 | fix(accessories): preserve live tuning poses and keep charms hanging down |
| `7ff0fc67` | `0525036a` | 2026-11-03 | fix(hud): restart kill cards after bot takeover |
| `3f7338db` | `bfb3c798` | 2026-11-03 | fix(client): restore charm rest and CT dropped bomb radar |
| `64296ffe` | `172c278c` | 2026-11-03 | fix(charm): recover the idle reference after draw avoidance |
| `131dcfaf` | `cbe8511d` | 2026-11-03 | fix(accessories): recover mounted charms across weapon animations |
| `68e79fd7` | `de94cfb9` | 2026-11-03 | perf(inventory): prioritize and retain weapon appearance textures |
| `856aa8f7` | `83ad9310` | 2026-11-04 | fix(hud): stabilize optical dots and restore left radio icons |
| `01124367` | `cf518a43` | 2026-11-04 | fix(client): unify AUG and SG552 small green optic dots |
| `5db5b62b` | `b3da38fb` | 2026-11-04 | fix(client): keep Elite magazines seated across reload transitions |
| `cf21ddad` | `3df6a6bc` | 2026-11-04 | fix(client): play headshot confirmation without attenuation |
| `d360e384` | `4097bd3f` | 2026-11-04 | fix(client): randomize native headshot confirmation samples |
| `2993cb45` | `808604db` | 2026-11-04 | fix(client): finalize optic dot size for the OC2 release |
| `26f633e2` | `7cdda7cc` | 2026-11-04 | chore(release): record the pickup landing sound hotfix |
| `9690a3fe` | `e74fc5dd` | 2026-11-04 | fix(client): hide charms on dropped world weapons |
| `b103412f` | `8d9bc855` | 2026-11-04 | fix(client): clarify scoreboard labels and hide carried charms |
| `9cc380c1` | `8dbb1402` | 2026-11-04 | fix(client): reset texture upload pacing after video restart |
| `27762026` | `93221f29` | 2026-11-04 | fix(client): show HE and flash on rear carry slots |
| `b528aec1` | `464b042f` | 2026-11-04 | fix(client): stagger paired flash carry positions |
| `2ec7b463` | `1164883a` | 2026-11-04 | fix(client): articulate ragdolls and unbury carried utilities |
| `5b5474f5` | `9f03b695` | 2026-11-04 | tune(client): align carried utilities in one horizontal row |
| `277cf942` | `5b6b32c2` | 2026-11-04 | module6: Havana ragdoll integration, random carried slots, taser mount |

</details>

<details><summary><b>xash3d-fwgs</b>（24 条）</summary>

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
| `f5bf0c65` | `cfcf029c` | 2026-10-27 | fix(render): restore handedness preference for C4 |
| `a37cbf81` | `31124f12` | 2026-10-29 | perf(studio): batch oversized meshes at primitive boundaries |
| `8ccc04d4` | `cbc0666d` | 2026-11-02 | perf(engine): cache studio meshes and keep radar blur on GPU |
| `2df6fa1a` | `d1daa3b6` | 2026-11-02 | chore(renderer): trace cold model and scene rendering stalls |
| `77128a1a` | `cd1f8417` | 2026-11-02 | perf(renderer): index validated studio mesh cache hits |
| `85082ba6` | `c348ee19` | 2026-11-02 | fix(audio): keep music playing through UI stalls and enable x86 large addresses |
| `80724eb6` | `dcadb61f` | 2026-11-03 | perf(render): stream prepared inventory textures in bounded chunks |
| `709fea44` | `d1e91f75` | 2026-11-03 | fix(input): show native IME candidate windows |
| `d1044022` | `ac7299dd` | 2026-11-03 | fix(renderer): keep remote muzzle lights in world space |
| `a5690909` | `ca878dd9` | 2026-11-03 | fix(render): stabilize charm artwork at depth precision limits |
| `e77ace42` | `b99e1315` | 2026-11-03 | fix(render): preserve filtered charm artwork coverage |
| `7fe370c2` | `59e265e0` | 2026-11-04 | fix(renderer): exclude live weapons from inventory previews |

</details>

<details><summary><b>regamedll</b>（41 条）</summary>

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
| `ba4322fa` | `4c5b0e7b` | 2026-10-27 | fix(server): finish held reload and cancel pending Nova casing |
| `6b875a40` | `fe7c72a8` | 2026-10-28 | feat: synchronize carried equipment state |
| `2b91efe1` | `44828151` | 2026-10-29 | test(carry): expose real utility acquisition and carried state |
| `79d0dfb9` | `8c4aac18` | 2026-10-29 | feat(items): support grenade drops and bullet-driven weapon physics |
| `8ec1966e` | `c3b9a9a7` | 2026-10-29 | perf(items): speed up grounded weapon rest alignment |
| `a72de34f` | `71597a5c` | 2026-10-29 | perf(items): tighten grounded weapon rest gates |
| `635e7942` | `5a3a0cee` | 2026-10-30 | feat(hostage): add carrier lifecycle and authoritative rescue outcomes |
| `e79f2851` | `ed82ce2d` | 2026-10-31 | fix(hostage): add timed rescue kits and protect hostages |
| `6c2f47dc` | `63dee4c5` | 2026-10-31 | restore wood and grass footstep materials |
| `fb8c5d32` | `1bf20d63` | 2026-10-31 | fix(hostage): preserve hit feedback and filter T radar |
| `6f49e43b` | `b2d26bc5` | 2026-10-31 | feat(spectator): publish observer snapshots and statistics |
| `23e7eb51` | `9b615394` | 2026-11-01 | feat(c4): use separate dropped weaponbox model |
| `4980fb73` | `80bb50bd` | 2026-11-01 | fix(purchase): use weapon cue for Zeus and defuse kits |
| `138b5aee` | `114e2bde` | 2026-11-01 | feat(zeus): allow rebuying a spent Zeus in the same round |
| `52297cb3` | `561013e0` | 2026-11-01 | fix(spectator): distinguish map-specific CT rescue kits |
| `afd45ef9` | `0848a93a` | 2026-11-01 | feat(pickup): add transactional use-key weapon pickup |
| `98c779af` | `5ade1563` | 2026-11-01 | feat(server): add bot takeover and shared grenade capacity |
| `ec03799b` | `f4d0fc87` | 2026-11-01 | feat(match): align competitive rules, economy and tactical bots |
| `6c1481a9` | `eec7eae3` | 2026-11-02 | fix(server): repair smoke lifetime state and bot initialization |
| `8f99dbdb` | `55adc934` | 2026-11-02 | fix(server): restore bot throws and timely spectator snapshots |
| `65db9db1` | `a51ec900` | 2026-11-02 | fix(server): guard ownerless grenade materialization |
| `9521def6` | `f5479f3e` | 2026-11-02 | feat(bots): coordinate utility and preserve individual loadouts |
| `fa2f942b` | `161f0b79` | 2026-11-03 | feat(bots): share mount layouts and gate utility buying at 20 percent |
| `4988c44b` | `9a714079` | 2026-11-03 | fix(match): return local matches to the lobby after results |
| `4e9d7943` | `2f27520c` | 2026-11-03 | fix(radar): share spotted dropped C4 with CT teammates |
| `b4b0ddc1` | `31a62244` | 2026-11-03 | fix(game): sync helmets and play voluntary drop landing audio |
| `afadf5f1` | `e6c39462` | 2026-11-04 | fix(game): cover purchase drops and default death to first person |
| `fb727341` | `40f05184` | 2026-11-04 | fix(server): play pickup landing sound only for discarded guns |
| `1e3a8d4b` | `6c576cf9` | 2026-11-04 | module6: carry taser state and fix spent Zeus crash |

</details>

<details><summary><b>mainui</b>（35 条）</summary>

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
| `ff0b0695` | `8769f8ff` | 2026-10-27 | fix(ui): align inventory previews and add lobby navigation |
| `4ab7fd39` | `6b04cfbc` | 2026-10-27 | fix(inventory): align sticker slots and unfold dual pistols |
| `ee2463cf` | `0ee9373c` | 2026-10-28 | fix(inventory): fit selected previews and hide XM shells |
| `c344348f` | `01d881bf` | 2026-11-01 | fix(inventory): fit long guns by mesh and rotate dual pistols whole |
| `2a141ee6` | `fbb15c42` | 2026-11-01 | tweak(inventory): nudge long guns outward and sync dual-pane orbit |
| `5ff7731f` | `742122ec` | 2026-11-01 | feat(inventory): rotate split Elite panes as one rigid pair |
| `65a33d84` | `14deafab` | 2026-11-01 | fix(inventory): arrange dual Berettas as one mirrored preview |
| `6b8c3406` | `67f04f05` | 2026-11-01 | fix(inventory): stabilize long-gun wheel preview sizing |
| `68d7c8c4` | `fd59dfd4` | 2026-11-01 | fix(inventory): align dual previews around one axis |
| `4dddeaa2` | `d7602012` | 2026-11-01 | fix(inventory): limit dual arrangement to inspect and keep distance |
| `b41a1a39` | `efdfaddf` | 2026-11-01 | fix(frontend): tune dual thumbnail and default entry pages |
| `acf69c0a` | `f6c245fd` | 2026-11-01 | tweak(frontend): nudge M249 wheel preview left |
| `f27d766d` | `864621ca` | 2026-11-01 | fix(play): fill ten competitive slots and allow full-team swaps |
| `a52562ab` | `e333c65c` | 2026-11-02 | perf(menu): cache inventory previews and add crosshair settings |
| `78f84466` | `ef06fbde` | 2026-11-02 | perf(menu): prewarm purchases and share streamed preview textures |
| `770f5310` | `14d97c9a` | 2026-11-02 | fix(menu): report expired explicit purchase requests |
| `b657b2f8` | `0d019359` | 2026-11-03 | feat(settings): add explicitly saved UTF-8 player nickname |
| `f7532f98` | `369e74a2` | 2026-11-03 | fix(menu): hide redundant charm pattern default |
| `486e21fb` | `b837f688` | 2026-11-03 | fix(menu): clarify charm pattern description |
| `268dcb8c` | `053bde13` | 2026-11-03 | feat(inventory): add 3D mount editing and shared bot defaults |
| `05ae74aa` | `265a3c73` | 2026-11-03 | fix(inventory): separate static personal and bot mount editing |
| `3bfae080` | `b3d271c8` | 2026-11-03 | fix(inventory): align M3 side preview with its gun frame |
| `ff6be71c` | `5efcc298` | 2026-11-03 | fix(inventory): show transparent default sticker and charm artwork |
| `fdcfd1ca` | `31ca4c19` | 2026-11-03 | fix(inventory): keep charm parameter labels clear of slider tracks |
| `20d0f9c3` | `0f980617` | 2026-11-03 | fix(inventory): synchronize dual pistol accessory presentation |

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
