#pragma once
// Wine 10 win32u/defwnd.c adjust_window_rect chooses the static outer
// border instead of the normal outer frame. A modal frame takes priority.
// Keep this backend's classic resize-border width of one pixel.
inline LONG classic_window_border(DWORD style, DWORD ex) {
 LONG border=0;
 if((ex&(WS_EX_STATICEDGE|WS_EX_DLGMODALFRAME))==WS_EX_STATICEDGE) border=1;
 else if((ex&WS_EX_DLGMODALFRAME)||(style&(WS_THICKFRAME|WS_DLGFRAME))) border=2;
 if(style&WS_THICKFRAME) ++border;
 if((style&(WS_BORDER|WS_DLGFRAME))||(ex&WS_EX_DLGMODALFRAME)) ++border;
 return border;
}
