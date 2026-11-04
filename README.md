# OC2 · CS 1.6 (Xash3D)

在 **Xash3D** 引擎上把 **CS 1.6** 改造成现在 **CS2** 那种风格 —— 这就是本仓库的源码。

## 分支

- `cs16-client`, `xash3d-fwgs`, `regamedll`, `mainui`, `cssdk` —— **精简版**：只保留我的提交，每棵树只含我实际改过的文件，便于逐处审阅。
- `full/cs16-client`, `xash3d-fwgs`, `regamedll`, `mainui`, `cssdk` —— **完整版**：提交历史相同，但每棵树是完整项目代码（我改过的子模块已内嵌展开），可直接 clone 编译。

上游历史已剥离；作者、日期、提交信息原样保留。

## 提交对照（原提交 → 精简提交 / 完整提交）

每个仓的提交按原顺序一一对应。

<details><summary><b>cs16-client</b>（123 条）</summary>

| 原提交 | 精简提交 | 完整提交 | 日期 | 说明 |
|---|---|---|---|---|
| `7bc5cd02` | `19fb6a62` | `21518af5` | 2026-10-11 | chore(oc2): pin regamedll fix, ignore build_regamedll |
| `d03f08a8` | `1e271092` | `abf5c1ab` | 2026-10-11 | feat(radar): add CHudRadarModern (CS:GO radar) reconstructed from the apk |
| `e6741163` | `e132fc2f` | `bda5669a` | 2026-10-11 | radar: anchor overview on the player, unify blip transform |
| `3890e983` | `2999d644` | `edd60e5d` | 2026-10-11 | radar: fix anchor double-count, bind white for the disc, clip tiles to the circle |
| `1929d464` | `f5141c5f` | `c1c177fc` | 2026-10-11 | radar: turn the map with the view, navy plate, self marker |
| `5f13e2ea` | `4373736b` | `20d5bfe1` | 2026-10-11 | radar: the overview origin is the image's centre-x / top edge, not its corner |
| `25e420bd` | `2351a445` | `69156f6a` | 2026-10-11 | radar: rewrite the mapping in tile space, per the finished reverse engineering |
| `43509950` | `4f938f84` | `42e05876` | 2026-10-11 | radar: fix the tile mapping - axes are not crossed, X sign was flipped |
| `6355f36e` | `e1498a36` | `58876994` | 2026-10-11 | radar: expose the residual tile offset, make the centre marker unconditional |
| `52ebdca2` | `c2209cde` | `3d6f262b` | 2026-10-11 | radar: derive the world->overview mapping from the map bounds |
| `4706f5af` | `684a6330` | `548e4169` | 2026-10-11 | radar: point the self arrow the way the player faces on the rotated grid |
| `c8ac62b5` | `aa950a3b` | `e8680457` | 2026-10-11 | radar: blip origins, radar zoom and the invisible self arrow |
| `485936c1` | `4ebfb3d7` | `d75bab7f` | 2026-10-11 | radar: derive the self arrow from the grid basis |
| `5f7d3454` | `7258d0a1` | `91e3fa1e` | 2026-10-11 | radar: the tile column takes world y, the tile row takes world x |
| `dbd6aaab` | `d8f199a9` | `54f4700d` | 2026-10-12 | radar: rotate the map by yaw+90, feed the self arrow tile-space facing |
| `252d653b` | `7755e8bf` | `9d779426` | 2026-10-12 | radar: drop blips outside the circle, teammates blue and enemies yellow |
| `0bdbc74e` | `fa379779` | `22328e18` | 2026-10-12 | radar: take the blip team from the entity state, enemies red |
| `b43cdc11` | `4e33fb7d` | `a569c02b` | 2026-10-12 | radar: get the team over the one channel that actually reaches the client |
| `e8a35dea` | `b767b6ed` | `e9fceb81` | 2026-10-12 | radar: fix ROTATED overview mapping and derive the self arrow from it |
| `724bf2cc` | `9f517cb2` | `a9af598b` | 2026-10-13 | radar: gate enemy blips behind a real line-of-sight test |
| `7c39b109` | `72eacd7f` | `e32e2d97` | 2026-10-13 | radar: acceptance build - circular frame and blurred backdrop |
| `335ac61f` | `3261c5b9` | `c5bc063b` | 2026-10-13 | hud: CS:GO view bob behind cl_bobstyle 2 |
| `ae97463c` | `ceb61fc2` | `ebb473cd` | 2026-10-13 | viewmodel: CS:GO view bob instead of the GoldClient fork |
| `6223daf3` | `4227ddf9` | `9853bf72` | 2026-10-13 | viewmodel: stop the bob stuttering on ramps and stairs |
| `5d1147d3` | `f40e518f` | `5f85a064` | 2026-10-13 | viewmodel: CS:GO weapon sway, and stop the lateral bob vanishing on pistols |
| `e4d55f23` | `d7a3219b` | `7f133b78` | 2026-10-14 | fix(hud): restore viewmodel effects and smooth downhill camera |
| `f38d9c30` | `c9cf17cd` | `67dddc8a` | 2026-10-14 | fix(input): honor fast switch for wheel weapon selection |
| `67bae185` | `cf0e126f` | `5d5ebf22` | 2026-10-14 | docs: keep project notes in the canonical md directory |
| `e155f107` | `d242398d` | `5c08f705` | 2026-10-15 | feat(ui): integrate avatars, match HUD and purchase menu |
| `b8335f51` | `381778b3` | `cb3d9e74` | 2026-10-16 | feat(equipment): complete weapons, sights and HUD feedback |
| `20726a48` | `c110f6de` | `8618ab05` | 2026-10-18 | Complete module 3 HUD, aiming and weapon flow fixes |
| `08128beb` | `ee95172d` | `a92d4cec` | 2026-10-19 | Finish model ADS polish and Source-style viewmodel controls |
| `56e4e9ea` | `ef105a17` | `fae9ceb1` | 2026-10-19 | Stabilize viewmodel motion, reload playback and muzzle effects |
| `22e4db38` | `65f20c63` | `5cf4b3e4` | 2026-10-21 | feat(inventory): attach counters and sticker charms to weapon surfaces |
| `107065f2` | `7555b210` | `de4779ff` | 2026-10-21 | fix(inventory): refresh saved accessory placement without stale anchors |
| `ea8c44d5` | `aad6a828` | `f8a3db77` | 2026-10-23 | fix(attachments): preserve mounts and constrain charm swing |
| `9924526c` | `d5dca800` | `516f721e` | 2026-10-24 | feat(inventory): render persistent weapon and knife cosmetics |
| `cab9d379` | `3ddc0bad` | `96863f8a` | 2026-10-24 | feat(inventory): complete cosmetics and multiple inspect animations |
| `cf8696f3` | `90624e69` | `7fdad0d4` | 2026-10-26 | feat(client): improve combat effects and continuous body animation |
| `27fd257f` | `3255dbde` | `f41c3400` | 2026-10-26 | fix(animation): update layered first-person body with current view |
| `d6d31c97` | `01640f5f` | `cf648014` | 2026-10-26 | fix(client): keep first-person body intact through jumping |
| `3840fd81` | `164885ec` | `171bc0cd` | 2026-10-26 | fix(client): align body framing and Source weapon motion |
| `d93c9781` | `0b87b1f4` | `8589d4fd` | 2026-10-26 | fix(client): preserve body placement and natural ragdoll falls |
| `62e41539` | `c6eb2bc8` | `019c1a11` | 2026-10-26 | fix(client): stabilize jump torso and predicted crouch body |
| `1687b1d4` | `53b8e4c9` | `cdf4d3c8` | 2026-10-26 | fix(client): stabilize reload and body poses with world gloves |
| `768f282f` | `f61cfccb` | `c7ad2dc2` | 2026-10-27 | fix(client): unify actors and improve death and HUD presentation |
| `702265f7` | `44e4dfbf` | `23e154c3` | 2026-10-27 | fix(client): improve shell ejection and animation transitions |
| `0d82e077` | `99bf9766` | `b3047c78` | 2026-10-27 | fix(hud): fit bomb icons and raise animated notices |
| `551cb5dc` | `4a37d553` | `61a2c93f` | 2026-10-27 | fix(inventory): update menu submodule for sticker ordering |
| `381506fe` | `ddcd423c` | `80a1da75` | 2026-10-28 | fix(inventory): update menu pointer for preview refinements |
| `4b5834e8` | `1fe8c76a` | `8da0c67e` | 2026-10-28 | fix(viewmodel): smooth aiming motion and interrupted transitions |
| `de26abd8` | `0099b6d3` | `493a050c` | 2026-10-28 | fix(hud): restore radio menu and equipment presentation |
| `44f9a35f` | `1ef4f7c3` | `b83a6672` | 2026-10-28 | feat: display carried weapons and equipment |
| `bc8c88ff` | `3dbc234a` | `2fc1d526` | 2026-10-29 | perf(inventory): stabilize carried skins and world sticker rendering |
| `f87f9146` | `8a750f49` | `7305c960` | 2026-10-29 | chore(server): advance pickup and dropped-item physics |
| `2533a74b` | `e4881d2d` | `6e31f2aa` | 2026-10-29 | chore(server): advance grounded weapon rest alignment |
| `19e06393` | `62123fb8` | `c5476f81` | 2026-10-29 | chore(server): advance grounded weapon rest gates |
| `7a916096` | `3ba37e26` | `5717eaac` | 2026-10-29 | feat(radar): show bomb sites, hostages and the C4 carrier correctly |
| `274c9c48` | `c89be57f` | `451ae2a3` | 2026-10-30 | feat(radar): enemy ghosts, death X and sight windows |
| `23866f31` | `de0ba918` | `1c5d0773` | 2026-10-30 | fix(radar): restore teammate and enemy blips |
| `d4255fe6` | `8f6018b5` | `16d92692` | 2026-10-30 | feat(radar): real death X, C4 carrier and dropped-bomb markers |
| `ef121601` | `8eaca68e` | `4e3b4a16` | 2026-10-30 | style(radar): shrink C4 markers to blip size |
| `ab506692` | `7b2e33e1` | `96de18d6` | 2026-10-30 | fix(radar): 修复标记偏移、阵营配色与 C4 图标显示 |
| `0f2e75c3` | `edb18740` | `f1f83928` | 2026-10-30 | fix(radar): 掉落 C4 改用箱子实体定位，去掉移动丢弃的闪现与 1 秒空窗 |
| `9a294e68` | `ce50b212` | `ecee5d1a` | 2026-10-30 | fix(radar): 掉落C4按消息定位、队伍判定统一，修白边/红环/包点顺序 |
| `6fa5c3c2` | `9e4a1fa8` | `42bb84ef` | 2026-10-30 | fix(radar): preserve lifecycle state and reject stale bomb positions |
| `5864e37f` | `47af06a2` | `7e52b89e` | 2026-10-30 | feat(hostage): render carried hostages and synchronize radar states |
| `5eac2851` | `0d2ac8fd` | `be1135b3` | 2026-10-31 | fix(hostage): anchor rescue labels and show rescue equipment |
| `66dff788` | `ed16ae10` | `f4f0c649` | 2026-10-31 | restore wood and grass footstep materials |
| `15c3a047` | `97695f33` | `ea81c152` | 2026-10-31 | fix(radar): display filtered hostage states for T |
| `02117dfa` | `5fa9742e` | `b54cd441` | 2026-10-31 | feat(hud): redesign spectator UI and full-map radar |
| `a1fdad90` | `d9b2e64c` | `98ea4105` | 2026-11-01 | fix(spectator): recognize dropped C4 model |
| `faa4442f` | `7f5f6e96` | `3dcdabde` | 2026-11-01 | chore(submodule): advance purchase sound build |
| `57b8ea5f` | `24367991` | `9fd6d1bb` | 2026-11-01 | fix(buymenu): re-enable the Zeus once the held one is spent |
| `336481a5` | `457083c4` | `83418833` | 2026-11-01 | chore(submodule): advance inventory wheel and dual-pistol preview |
| `938dc76c` | `04df97b8` | `e498323d` | 2026-11-01 | chore(submodule): advance wheel nudge and dual-pane rotation |
| `c4d61fe0` | `60460e00` | `52529af9` | 2026-11-01 | chore(submodule): advance rigid dual-pane preview |
| `cd6241e3` | `64cb1070` | `9f07d0a7` | 2026-11-01 | chore(submodule): advance one-view Elite preview and wheel cache |
| `e73e0165` | `8a5d6fe6` | `d1ecf512` | 2026-11-01 | chore(submodule): advance dual-axis alignment |
| `eafddccd` | `f4dedb06` | `e359a6ad` | 2026-11-01 | chore(submodule): scope dual layout to inspect |
| `fda8b121` | `57683a42` | `322f4342` | 2026-11-01 | chore(submodule): advance frontend entry defaults |
| `5ff130ca` | `a84bb0f8` | `be82b9f5` | 2026-11-01 | chore(submodule): advance M249 wheel nudge |
| `094d8137` | `7dec0eb5` | `3fbfb54a` | 2026-11-01 | fix(spectator): refine observer layout and equipment icons |
| `7cea5ad8` | `e1a62b30` | `f35179b8` | 2026-11-01 | chore(server): pin verified native use pickup |
| `8d878619` | `5019048f` | `b00ca6f1` | 2026-11-01 | feat(client): adapt observer controls and bot appearance transfer |
| `59b33653` | `bc73be37` | `16db9295` | 2026-11-01 | feat(hud): add AI portraits and refine combat feedback |
| `e743d4eb` | `4ac4d1d8` | `6656734f` | 2026-11-02 | perf(client): stream cosmetics and finish combat presentation |
| `64d2d01d` | `6fa211e8` | `c044a172` | 2026-11-02 | fix(client): smooth match transitions and bound stage notices |
| `c1cddcb9` | `aedd0d11` | `5eb71ddc` | 2026-11-02 | fix(client): unify center feedback and retain M4 charm colliders |
| `9cc456b8` | `1f1ff452` | `99e2444e` | 2026-11-02 | perf(client): bound charm work and add individual bot profiles |
| `011a74d6` | `fa8cdfd3` | `fa63371e` | 2026-11-03 | fix(client): validate radar and corpse snapshots and cache appearances |
| `921adbfe` | `83d79c11` | `ffca4516` | 2026-11-03 | fix(hud): unify spectator notices and clarify observer information |
| `c876a028` | `b2a3a510` | `59348571` | 2026-11-03 | fix(menu): update charm selection UI |
| `70f64f00` | `de5158af` | `4173194f` | 2026-11-03 | fix(menu): update charm description font |
| `48ee3660` | `6cf7e856` | `92f57bbc` | 2026-11-03 | fix(appearance): calibrate side mounts and map saved positions to world guns |
| `99ca8e9c` | `0b89c339` | `f7f4e704` | 2026-11-03 | feat(accessories): persist editable mount defaults across views |
| `b41b21e7` | `877a90ea` | `03143f72` | 2026-11-03 | fix(inventory): preserve pending charm image selections |
| `987ee3f6` | `9080ead9` | `5d0cd00a` | 2026-11-03 | fix(inventory): update static mount editor submodule |
| `4ed26206` | `eed1dc5b` | `17671c52` | 2026-11-03 | fix(inventory): update M3 preview alignment submodule |
| `17396641` | `582b6e5c` | `5422ddca` | 2026-11-03 | fix(inventory): make default weapon and charm artwork transparent |
| `97b09684` | `952af155` | `ae95de5c` | 2026-11-03 | fix(inventory): update charm parameter spacing submodule |
| `1db9308e` | `54b57ad8` | `3d18f69f` | 2026-11-03 | fix(game): update dual pistol presentation and match lobby submodules |
| `5bcdce2e` | `4dc47623` | `652167a2` | 2026-11-03 | fix(accessories): preserve live tuning poses and keep charms hanging down |
| `7ff0fc67` | `0525036a` | `21ce5a23` | 2026-11-03 | fix(hud): restart kill cards after bot takeover |
| `3f7338db` | `bfb3c798` | `3dbf541f` | 2026-11-03 | fix(client): restore charm rest and CT dropped bomb radar |
| `64296ffe` | `172c278c` | `0d1a20c9` | 2026-11-03 | fix(charm): recover the idle reference after draw avoidance |
| `131dcfaf` | `cbe8511d` | `9537af26` | 2026-11-03 | fix(accessories): recover mounted charms across weapon animations |
| `68e79fd7` | `de94cfb9` | `ac18a656` | 2026-11-03 | perf(inventory): prioritize and retain weapon appearance textures |
| `856aa8f7` | `83ad9310` | `72961935` | 2026-11-04 | fix(hud): stabilize optical dots and restore left radio icons |
| `01124367` | `cf518a43` | `6c538a1b` | 2026-11-04 | fix(client): unify AUG and SG552 small green optic dots |
| `5db5b62b` | `b3da38fb` | `efa4ce01` | 2026-11-04 | fix(client): keep Elite magazines seated across reload transitions |
| `cf21ddad` | `3df6a6bc` | `f15b445c` | 2026-11-04 | fix(client): play headshot confirmation without attenuation |
| `d360e384` | `4097bd3f` | `6d4dca8c` | 2026-11-04 | fix(client): randomize native headshot confirmation samples |
| `2993cb45` | `808604db` | `25ec524c` | 2026-11-04 | fix(client): finalize optic dot size for the OC2 release |
| `26f633e2` | `7cdda7cc` | `9bca6d84` | 2026-11-04 | chore(release): record the pickup landing sound hotfix |
| `9690a3fe` | `e74fc5dd` | `c8a70403` | 2026-11-04 | fix(client): hide charms on dropped world weapons |
| `b103412f` | `8d9bc855` | `eaefcc68` | 2026-11-04 | fix(client): clarify scoreboard labels and hide carried charms |
| `9cc380c1` | `8dbb1402` | `661797d0` | 2026-11-04 | fix(client): reset texture upload pacing after video restart |
| `27762026` | `93221f29` | `32e0bad2` | 2026-11-04 | fix(client): show HE and flash on rear carry slots |
| `b528aec1` | `464b042f` | `01908384` | 2026-11-04 | fix(client): stagger paired flash carry positions |
| `2ec7b463` | `1164883a` | `300a2950` | 2026-11-04 | fix(client): articulate ragdolls and unbury carried utilities |
| `5b5474f5` | `9f03b695` | `47b8399d` | 2026-11-04 | tune(client): align carried utilities in one horizontal row |
| `277cf942` | `5b6b32c2` | `ffd2e994` | 2026-11-04 | module6: Havana ragdoll integration, random carried slots, taser mount |

</details>

<details><summary><b>xash3d-fwgs</b>（24 条）</summary>

| 原提交 | 精简提交 | 完整提交 | 日期 | 说明 |
|---|---|---|---|---|
| `d1ae2d10` | `672055c8` | `d2425eb6` | 2026-10-11 | feat(radar): engine-side CS:GO radar background blur API |
| `086af360` | `c389b48a` | `a90af129` | 2026-10-11 | radar: export the BSP world-model bounds to the client |
| `7f9a50d4` | `05e2aeea` | `28a9d8d7` | 2026-10-12 | engine: let a windowed listen server actually honour maxplayers and '+' commands |
| `cdd3008e` | `6c1713c5` | `208c1a9d` | 2026-10-12 | radar: get the team over the one channel that actually reaches the client |
| `dc96a658` | `2330c293` | `8b5f9bc9` | 2026-10-13 | radar: capture the blur backdrop before the HUD is composited |
| `5b539e6b` | `8c991d13` | `39ab8fc5` | 2026-10-15 | fix(ui): expose session identity and isolate preview mirroring |
| `671ce415` | `c1a1633e` | `1e64c103` | 2026-10-16 | fix(render): unify viewmodel handedness and preserve menu wheel input |
| `27fc012d` | `f210dc2f` | `3b47c0c2` | 2026-10-19 | Support independent viewmodel FOV projection |
| `a6d91b73` | `36e412e4` | `a84da57a` | 2026-10-24 | feat(ui): isolate native model preview rendering |
| `a541f377` | `ebada6d8` | `4fea8270` | 2026-10-26 | fix(render): composite flash blindness after all interface layers |
| `9cabbba5` | `92696572` | `cd58bc3a` | 2026-10-26 | feat(console): add a movable native console window |
| `d59b7b23` | `bc101de0` | `9322ad69` | 2026-10-27 | feat(ui): adopt Source-style console and loading presentation |
| `f5bf0c65` | `cfcf029c` | `9f31b91b` | 2026-10-27 | fix(render): restore handedness preference for C4 |
| `a37cbf81` | `31124f12` | `46e14b35` | 2026-10-29 | perf(studio): batch oversized meshes at primitive boundaries |
| `8ccc04d4` | `cbc0666d` | `70a26de0` | 2026-11-02 | perf(engine): cache studio meshes and keep radar blur on GPU |
| `2df6fa1a` | `d1daa3b6` | `aeebb771` | 2026-11-02 | chore(renderer): trace cold model and scene rendering stalls |
| `77128a1a` | `cd1f8417` | `0ccf940b` | 2026-11-02 | perf(renderer): index validated studio mesh cache hits |
| `85082ba6` | `c348ee19` | `fd4bf55d` | 2026-11-02 | fix(audio): keep music playing through UI stalls and enable x86 large addresses |
| `80724eb6` | `dcadb61f` | `2a9a7be9` | 2026-11-03 | perf(render): stream prepared inventory textures in bounded chunks |
| `709fea44` | `d1e91f75` | `016fc358` | 2026-11-03 | fix(input): show native IME candidate windows |
| `d1044022` | `ac7299dd` | `6b4cb614` | 2026-11-03 | fix(renderer): keep remote muzzle lights in world space |
| `a5690909` | `ca878dd9` | `77e8ac1a` | 2026-11-03 | fix(render): stabilize charm artwork at depth precision limits |
| `e77ace42` | `b99e1315` | `6da8712a` | 2026-11-03 | fix(render): preserve filtered charm artwork coverage |
| `7fe370c2` | `59e265e0` | `e727c3ff` | 2026-11-04 | fix(renderer): exclude live weapons from inventory previews |

</details>

<details><summary><b>regamedll</b>（41 条）</summary>

| 原提交 | 精简提交 | 完整提交 | 日期 | 说明 |
|---|---|---|---|---|
| `fb788629` | `678bbab1` | `56d47145` | 2026-10-11 | fix(server): disable file consistency check for offline play |
| `732bcdbf` | `c60f8ee4` | `724d8ccf` | 2026-10-12 | radar: get the team over the one channel that actually reaches the client |
| `11788f75` | `c6c94f88` | `b8a45a8a` | 2026-10-15 | feat(hud): synchronize round resets and purchase eligibility |
| `b5ae1a60` | `d68b5707` | `fde91eaa` | 2026-10-16 | feat(equipment): complete modern purchases and weapon presentation |
| `df514a61` | `3821fcab` | `7d2ae5ba` | 2026-10-18 | Complete module 3 weapon parameters and reload timing |
| `dae5668c` | `847268d5` | `55f48ccf` | 2026-10-19 | Fix weapon aim speed, reload feedback and round ammo stock |
| `77c20813` | `68722673` | `6f54e034` | 2026-10-19 | Fix purchase animation triggers and per-round Glock feedback |
| `7fb99ea3` | `7ac53fe6` | `59ac95cf` | 2026-10-24 | feat(inventory): match equipped knife animation durations |
| `b7f735b1` | `88a0509d` | `98115daa` | 2026-10-24 | feat(inventory): sync bot cosmetics and player gloves |
| `8978885a` | `144f6526` | `20fa15a4` | 2026-10-26 | fix(weapons): correct delayed casing origin and facing |
| `41f8be21` | `58ff6a5f` | `31e668bb` | 2026-10-26 | feat(inventory): assign and replicate bot glove loadouts |
| `2421bb48` | `9b8dcc0d` | `340cf3f8` | 2026-10-27 | fix(player): use lobby-matched combat models for both teams |
| `ba4322fa` | `4c5b0e7b` | `10fc6ed5` | 2026-10-27 | fix(server): finish held reload and cancel pending Nova casing |
| `6b875a40` | `fe7c72a8` | `2b39fe02` | 2026-10-28 | feat: synchronize carried equipment state |
| `2b91efe1` | `44828151` | `29b8b4ae` | 2026-10-29 | test(carry): expose real utility acquisition and carried state |
| `79d0dfb9` | `8c4aac18` | `6258ec80` | 2026-10-29 | feat(items): support grenade drops and bullet-driven weapon physics |
| `8ec1966e` | `c3b9a9a7` | `6d475624` | 2026-10-29 | perf(items): speed up grounded weapon rest alignment |
| `a72de34f` | `71597a5c` | `92ab5b64` | 2026-10-29 | perf(items): tighten grounded weapon rest gates |
| `635e7942` | `5a3a0cee` | `3ffc3a80` | 2026-10-30 | feat(hostage): add carrier lifecycle and authoritative rescue outcomes |
| `e79f2851` | `ed82ce2d` | `6dfc877d` | 2026-10-31 | fix(hostage): add timed rescue kits and protect hostages |
| `6c2f47dc` | `63dee4c5` | `c24c17f7` | 2026-10-31 | restore wood and grass footstep materials |
| `fb8c5d32` | `1bf20d63` | `48a07cb0` | 2026-10-31 | fix(hostage): preserve hit feedback and filter T radar |
| `6f49e43b` | `b2d26bc5` | `3e23e53f` | 2026-10-31 | feat(spectator): publish observer snapshots and statistics |
| `23e7eb51` | `9b615394` | `a0e823d1` | 2026-11-01 | feat(c4): use separate dropped weaponbox model |
| `4980fb73` | `80bb50bd` | `d65fad21` | 2026-11-01 | fix(purchase): use weapon cue for Zeus and defuse kits |
| `138b5aee` | `114e2bde` | `9a7a9530` | 2026-11-01 | feat(zeus): allow rebuying a spent Zeus in the same round |
| `52297cb3` | `561013e0` | `e02fd8dc` | 2026-11-01 | fix(spectator): distinguish map-specific CT rescue kits |
| `afd45ef9` | `0848a93a` | `1b10ad6b` | 2026-11-01 | feat(pickup): add transactional use-key weapon pickup |
| `98c779af` | `5ade1563` | `4c1df72c` | 2026-11-01 | feat(server): add bot takeover and shared grenade capacity |
| `ec03799b` | `f4d0fc87` | `e5dea1d9` | 2026-11-01 | feat(match): align competitive rules, economy and tactical bots |
| `6c1481a9` | `eec7eae3` | `ff87e1ef` | 2026-11-02 | fix(server): repair smoke lifetime state and bot initialization |
| `8f99dbdb` | `55adc934` | `195ecf2f` | 2026-11-02 | fix(server): restore bot throws and timely spectator snapshots |
| `65db9db1` | `a51ec900` | `ecadee53` | 2026-11-02 | fix(server): guard ownerless grenade materialization |
| `9521def6` | `f5479f3e` | `0f388e4d` | 2026-11-02 | feat(bots): coordinate utility and preserve individual loadouts |
| `fa2f942b` | `161f0b79` | `3011a210` | 2026-11-03 | feat(bots): share mount layouts and gate utility buying at 20 percent |
| `4988c44b` | `9a714079` | `b1a149b1` | 2026-11-03 | fix(match): return local matches to the lobby after results |
| `4e9d7943` | `2f27520c` | `835142b2` | 2026-11-03 | fix(radar): share spotted dropped C4 with CT teammates |
| `b4b0ddc1` | `31a62244` | `71e367ed` | 2026-11-03 | fix(game): sync helmets and play voluntary drop landing audio |
| `afadf5f1` | `e6c39462` | `fc0b991f` | 2026-11-04 | fix(game): cover purchase drops and default death to first person |
| `fb727341` | `40f05184` | `6aa1a1fb` | 2026-11-04 | fix(server): play pickup landing sound only for discarded guns |
| `1e3a8d4b` | `6c576cf9` | `063b717b` | 2026-11-04 | module6: carry taser state and fix spent Zeus crash |

</details>

<details><summary><b>mainui</b>（35 条）</summary>

| 原提交 | 精简提交 | 完整提交 | 日期 | 说明 |
|---|---|---|---|---|
| `85f2d3ca` | `70148cf4` | `48d58a9d` | 2026-10-15 | feat(ui): add five-column buy menu and player previews |
| `431e420b` | `d7025c89` | `5a99d79d` | 2026-10-16 | feat(buy-menu): add continuous scrolling and modern equipment cards |
| `6105c923` | `8a0b5294` | `650f9c0d` | 2026-10-18 | Refine OC2 buy menu scrolling and character proportions |
| `d666fb65` | `f5b39189` | `8aed2246` | 2026-10-21 | feat(inventory): add modern frontend and weapon accessory editor |
| `931b29b1` | `a170c100` | `f5d0928c` | 2026-10-21 | fix(inventory): activate edited weapon accessories when saving presets |
| `bdfbb873` | `d537df22` | `06a5edf1` | 2026-10-23 | fix(inventory): preview attachments at their actual size |
| `7f28f41d` | `daa7ca10` | `3117ea4b` | 2026-10-24 | feat(inventory): equip weapon finishes knives and sticker slots |
| `24db0f87` | `d1d8fda4` | `adaa4474` | 2026-10-24 | feat(ui): add panorama layouts and 3D inventory previews |
| `8e2e0728` | `1069e989` | `bce74bbe` | 2026-10-26 | feat(ui): refine Chinese inventory and local match presentation |
| `cc599cb7` | `91a8b471` | `7fa54533` | 2026-10-27 | feat(menu): show map loading and complete Unicode font glyphs |
| `ff0b0695` | `8769f8ff` | `6bfb949a` | 2026-10-27 | fix(ui): align inventory previews and add lobby navigation |
| `4ab7fd39` | `6b04cfbc` | `f67b9eba` | 2026-10-27 | fix(inventory): align sticker slots and unfold dual pistols |
| `ee2463cf` | `0ee9373c` | `3f22efe6` | 2026-10-28 | fix(inventory): fit selected previews and hide XM shells |
| `c344348f` | `01d881bf` | `c811d3a0` | 2026-11-01 | fix(inventory): fit long guns by mesh and rotate dual pistols whole |
| `2a141ee6` | `fbb15c42` | `d44ef103` | 2026-11-01 | tweak(inventory): nudge long guns outward and sync dual-pane orbit |
| `5ff7731f` | `742122ec` | `db142806` | 2026-11-01 | feat(inventory): rotate split Elite panes as one rigid pair |
| `65a33d84` | `14deafab` | `43c2ed42` | 2026-11-01 | fix(inventory): arrange dual Berettas as one mirrored preview |
| `6b8c3406` | `67f04f05` | `c0af56bb` | 2026-11-01 | fix(inventory): stabilize long-gun wheel preview sizing |
| `68d7c8c4` | `fd59dfd4` | `518d228d` | 2026-11-01 | fix(inventory): align dual previews around one axis |
| `4dddeaa2` | `d7602012` | `b29ce8b1` | 2026-11-01 | fix(inventory): limit dual arrangement to inspect and keep distance |
| `b41a1a39` | `efdfaddf` | `3f228302` | 2026-11-01 | fix(frontend): tune dual thumbnail and default entry pages |
| `acf69c0a` | `f6c245fd` | `c8a45fae` | 2026-11-01 | tweak(frontend): nudge M249 wheel preview left |
| `f27d766d` | `864621ca` | `7010f396` | 2026-11-01 | fix(play): fill ten competitive slots and allow full-team swaps |
| `a52562ab` | `e333c65c` | `30591d90` | 2026-11-02 | perf(menu): cache inventory previews and add crosshair settings |
| `78f84466` | `ef06fbde` | `3900c470` | 2026-11-02 | perf(menu): prewarm purchases and share streamed preview textures |
| `770f5310` | `14d97c9a` | `0c3db676` | 2026-11-02 | fix(menu): report expired explicit purchase requests |
| `b657b2f8` | `0d019359` | `1b93244f` | 2026-11-03 | feat(settings): add explicitly saved UTF-8 player nickname |
| `f7532f98` | `369e74a2` | `0e7265db` | 2026-11-03 | fix(menu): hide redundant charm pattern default |
| `486e21fb` | `b837f688` | `de3eb6d8` | 2026-11-03 | fix(menu): clarify charm pattern description |
| `268dcb8c` | `053bde13` | `bdedde00` | 2026-11-03 | feat(inventory): add 3D mount editing and shared bot defaults |
| `05ae74aa` | `265a3c73` | `6610443c` | 2026-11-03 | fix(inventory): separate static personal and bot mount editing |
| `3bfae080` | `b3d271c8` | `c7d293bc` | 2026-11-03 | fix(inventory): align M3 side preview with its gun frame |
| `ff6be71c` | `5efcc298` | `2cb39d9f` | 2026-11-03 | fix(inventory): show transparent default sticker and charm artwork |
| `fdcfd1ca` | `31ca4c19` | `071ffedf` | 2026-11-03 | fix(inventory): keep charm parameter labels clear of slider tracks |
| `20d0f9c3` | `0f980617` | `6ec1a912` | 2026-11-03 | fix(inventory): synchronize dual pistol accessory presentation |

</details>

<details><summary><b>cssdk</b>（2 条）</summary>

| 原提交 | 精简提交 | 完整提交 | 日期 | 说明 |
|---|---|---|---|---|
| `248cc8d2` | `d592a565` | `fc05eb5f` | 2026-10-16 | feat(equipment): extend shared weapon and inventory definitions |
| `e98a85c4` | `b888ab3b` | `23d55670` | 2026-10-18 | Align OC2 weapon limits with CS:GO equipment |

</details>

## 与上游的关系

本仓的提交链**不含上游历史**，所以它是**参考/审阅**用的，不是可直接编译的工作树；
要拿到可直接编译的完整代码，请用 `full/*` 分支。

```powershell
git clone --branch full/cs16-client https://github.com/ellingtoncody81-ctrl/oc2-reference.git
```

## 版权

`cl_dll/` 等路径下的第三方 GoldSrc 源码版权归各自作者所有，许可证遵循各上游仓库原状。
