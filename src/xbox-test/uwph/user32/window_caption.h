#pragma once
// WS_CAPTION is the composite WS_BORDER | WS_DLGFRAME, not a single bit.
// Wine 10 win32u/defwnd.c adjust_window_rect requires both bits before
// applying caption height. Either border alone must remain symmetric.
inline bool has_caption(DWORD style) {
 return (style & WS_CAPTION) == WS_CAPTION;
}
// Wine selects the small caption metric for WS_EX_TOOLWINDOW. These are
// the record backend's fixed classic 96 DPI heights, including the separator.
inline LONG classic_caption_height(DWORD style, DWORD ex) {
 return has_caption(style) ? ((ex & WS_EX_TOOLWINDOW) ? 17 : 19) : 0;
}
