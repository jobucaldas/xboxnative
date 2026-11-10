// SPDX-License-Identifier: GPL-3.0-or-later
// Xbox-inspired visual system: flat near-black surfaces, one green accent, white focus ring.
// Values are measured from the launcher mockups (.mockups/launcher/MEASUREMENTS.md, xbox.css and the
// .figcraft vector source); keep them in sync with those, not with guesses.
#pragma once
#include <cstdint>

namespace ui {
namespace theme {

constexpr uint32_t argb(unsigned a, unsigned r, unsigned g, unsigned b) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}
constexpr uint32_t rgb(unsigned r, unsigned g, unsigned b) { return argb(255, r, g, b); }

// ---- surfaces (flat; the only depth cue is page vs panel)
constexpr uint32_t page        = 0xFF101010;  // library / screen background
constexpr uint32_t panel       = 0xFF1F1F1F;  // cards, guide body, sheets
constexpr uint32_t panelFoot   = 0xFF1E1E1E;  // guide footer strip, one step darker
constexpr uint32_t card        = 0xFF1F1F1F;  // info cards
constexpr uint32_t tileBack    = 0xFF1A1A1A;  // art placeholder behind a capsule
constexpr uint32_t control     = 0xFF303030;  // search pill, selected row, sort control
constexpr uint32_t controlDark = 0xFF2A2A2A;  // icon-only buttons
constexpr uint32_t rowHover    = 0xFF333333;
constexpr uint32_t chip        = 0xFF2A2A2A;
constexpr uint32_t track       = 0xFF2E2E2E;  // progress bars, donut track
constexpr uint32_t line        = 0xFF292929;  // 1px sidebar/content divider
constexpr uint32_t separator   = 0xFF2C2C2C;  // 1px rule inside a panel
constexpr uint32_t iconTile    = 0xFF262626;  // achievement icon square

// ---- text
constexpr uint32_t text        = 0xFFFFFFFF;
constexpr uint32_t text2       = 0xFFE4E4E4;
constexpr uint32_t textDim     = 0xFFA8A8A8;
constexpr uint32_t textMuted   = 0xFF8A8A8A;
constexpr uint32_t icon        = 0xFFE4E4E4;  // icon-only button glyph
constexpr uint32_t iconMuted   = 0xFF8C8C8C;  // inactive guide tab
constexpr uint32_t hint        = 0xFF8A8A8A;  // controller-prompt labels
constexpr uint32_t glyph       = 0xFF8A8A8A;  // launcher prompt disc
constexpr uint32_t glyphLabel  = 0xFF101010;  // letter on that disc
constexpr uint32_t glyphBright = 0xFFEFEFEF;  // in-game prompt disc, knob
constexpr uint32_t onAccent    = 0xFFFFFFFF;

// ---- green: accent only (ring, underline, active bar, primary action, progress)
constexpr uint32_t accent      = 0xFF107C10;
constexpr uint32_t accentEdge  = 0xFF2E9A2E;  // 1px edge on a primary button
constexpr uint32_t accentHover = 0xFF3F9640;  // the mockup CSS primary fill
constexpr uint32_t accentSoft  = 0xFF7FD97F;
// Not in the measurements: WinUI's default error red, for destructive confirmations only.
constexpr uint32_t danger      = 0xFFC42B1C;

// ---- scrim (flat black, no gradient); 0.55 alpha matches the mockups
constexpr uint32_t scrim       = 0xFF000000;
constexpr uint8_t  scrimAlpha  = 140;

// ---- controller-button brand colours (the measured mockups are monochrome #8A8A8A;
// these are the Xbox face-button colours, use only where the design calls for colour)
constexpr uint32_t buttonA     = 0xFF107C10;
constexpr uint32_t buttonB     = 0xFFC42B1C;
constexpr uint32_t buttonX     = 0xFF0B7BC1;
constexpr uint32_t buttonY     = 0xFFF5B800;

// ---- focus ring: white, 3px, 6px outside the shape, radius +4
constexpr uint32_t focus       = 0xFFFFFFFF;
constexpr float    ringWidth   = 3.0f;
constexpr float    ringOffset  = 6.0f;
constexpr float    ringRadiusDelta = 4.0f;

// ---- corner radii (measured: panels 18, tiles/cards 8, controls 4, search pill 28)
constexpr float radiusControl = 4.0f;
constexpr float radiusTile    = 8.0f;
constexpr float radiusCard    = 8.0f;
constexpr float radiusGuide   = 16.0f;
constexpr float radiusPanel   = 18.0f;
constexpr float radiusPill    = 28.0f;
constexpr float radiusBadge   = 3.0f;
constexpr float radiusToast   = 32.0f;

// ---- type scale: px / weight, from the .figcraft text styles
struct Type { int px; int weight; };
constexpr Type title        {44, 600};  // screen title
constexpr Type screenTitle  {56, 600};  // game screen title
constexpr Type section      {32, 600};
constexpr Type tab          {32, 400};  // selected tab is 600
constexpr Type cardLabel    {32, 400};
constexpr Type button       {30, 600};
constexpr Type panelHeading {26, 600};
constexpr Type sidebar      {24, 400};
constexpr Type body         {24, 400};
constexpr Type tile         {22, 400};
constexpr Type count        {22, 400};
constexpr Type toastTitle   {22, 600};
constexpr Type hintText     {20, 400};
constexpr Type small        {19, 400};
constexpr Type caption      {18, 400};
constexpr Type toastSub     {18, 400};
constexpr Type micro        {16, 400};
constexpr Type badge        {15, 700};
constexpr Type clock        {36, 600};
constexpr Type sheetTitle   {36, 600};

// ---- layout numbers shared by screens (measured at 1920x1080)
constexpr int width  = 1920;
constexpr int height = 1080;
constexpr int margin = 64;
constexpr int hintBaseline = 1032;  // 20px hint text sits at y=1008

}  // namespace theme
}  // namespace ui
