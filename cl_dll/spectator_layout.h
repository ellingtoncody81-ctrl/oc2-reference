#pragma once

// Native adaptation of cs-hud's fennec CSS grid, pinned to
// drweissbrot/cs-hud@5595dd02d67f0ca674d96d8c629e067ec6528c1b.
// Sources: css/vars/sizes.css, top-bar, sidebars/sidebar/player, focused-player.
// ISC License
// Copyright (c) 2023 Pierre Borgmann <cs-hud@drweissbrot.net>
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

namespace spectator_layout
{
struct Rect { float x, y, w, h; };
struct Layout
{
    float scale, width, height, margin, bottom, rowWidth, rowHeight, rowGap;
    Rect top, team[2], score[2], clock, alive, focus, health, ammo, radar;
    float RowX(int side) const { return side ? width - margin - rowWidth : margin; }
    float RowTop(int shown, bool overflow = false) const
    {
        return height - bottom - shown * rowHeight - (shown > 0 ? shown - 1 : 0) * rowGap
            - (overflow ? 25.0f * scale : 0.0f);
    }
};
inline Layout Make(int width, int height, float minimumScoreWidth = 0)
{
    Layout l = {};
    if (width <= 0 || height <= 0) return l;
    const float sx = width / 1920.0f, sy = height / 1080.0f;
    // CSS uses height-based rem. Bound it by width on 4:3 and narrow windows
    // so both 435px sidebars and the 960px focused-player grid still fit.
    l.scale = sx < sy ? sx : sy;
    const float s = l.scale;
    l.width = float(width); l.height = float(height);
    l.margin = 20*s; l.bottom = 15*s;
    // User feedback: larger readable cards, with a dedicated equipment rail.
    l.rowWidth = 500*s; l.rowHeight = 96*s; l.rowGap = 6*s;
    l.top = {(width-960*s)*.5f, 15*s, 960*s, 46*s};
    const float team = l.top.w*20/53, center = l.top.w*13/53;
    float score = center/5; // CSS 1fr : 3fr : 1fr before min-content sizing.
    const float scoreLimit = (center-90*s)*.5f;
    if (minimumScoreWidth > score) score = minimumScoreWidth < scoreLimit ? minimumScoreWidth : scoreLimit;
    l.team[0] = {l.top.x, l.top.y, team, l.top.h};
    l.team[1] = {l.top.x+l.top.w-team, l.top.y, team, l.top.h};
    l.score[0] = {l.top.x+team, l.top.y, score, l.top.h};
    l.score[1] = {l.team[1].x-score, l.top.y, score, l.top.h};
    l.clock = {l.score[0].x+score, l.top.y, l.score[1].x-l.score[0].x-score, l.top.h};
    // Keep the project's established left-hand alive counter clear of the
    // engine FPS readout and right-hand kill feed.
    l.alive = {l.margin, l.top.y, 120*s, 58*s};
    float focusedWidth=960*s;
    const float available=width-2*(l.margin+l.rowWidth+20*s);
    if(available<focusedWidth)focusedWidth=available;
    const float focusedX=(width-focusedWidth)*.5f,wing=focusedWidth*16/67;
    l.focus = {focusedX+wing, height-l.bottom-90*s, focusedWidth-2*wing, 90*s};
    l.health = {focusedX, height-l.bottom-45*s, wing-15*s, 45*s};
    l.ammo = {l.focus.x+l.focus.w+15*s, l.health.y, wing-15*s, 45*s};
    // Preserve GoldSrc's verified tile projection. Only its observer viewport
    // changes; the player's circular radar and Tab layout remain independent.
    l.radar = {l.margin, 82*s, 435*s, 435*s};
    return l;
}
inline float Ratio(int health)
{
    return health <= 0 ? 0.0f : health >= 100 ? 1.0f : health*.01f;
}
struct HealthTrail
{
    int identity = 0, health = -1;
    float previous = 0, changed = -1, last = -1;
    float Update(int userId, int hp, float now)
    {
        const float current = Ratio(hp);
        if (identity != userId || last < 0 || now < last || hp < 0 || hp > health)
        {
            identity=userId; health=hp; previous=current; changed=now; last=now;
            return current;
        }
        float t=(now-changed-2.5f)/.3f;
        if (t < 0) t=0; if (t > 1) t=1;
        t=t*t*(3-2*t);
        const float displayed=previous+(Ratio(health)-previous)*t;
        if (hp != health)
        {
            previous=displayed; health=hp; changed=now;
            last=now;
            return previous;
        }
        last=now;
        return displayed;
    }
};
}
