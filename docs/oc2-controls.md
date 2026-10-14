# OC2 键位与切枪

- 鼠标侧键 4：按住行走；Shift：按住蹲下。
- 滚轮下/上：下一把/上一把武器。
- `hud_fastswitch 1`：滚轮直接切枪；`0`：保留菜单与点击确认。
- 将 `config/oc2_controls.cfg` 放入 `cstrike`，在 `userconfig.cfg` 中执行 `exec oc2_controls.cfg`。

2026-10-14：此前 HUD 与下坡相机修复已经用户确认并提交（`e4d55f2`）。
本轮 Win32 增量编译通过；实游验证滚轮双向、连续切枪和关闭快切后的点击确认均正常，启动后的两项键位正确。
