#pragma once
// Wine 10 win32u/sysparams.c get_system_metrics: borders and edges are
// fixed; captions/menu/frame depend on nonclient metrics. Match the record
// backend's existing classic 96 DPI geometry rather than the host theme.
// Unknown indices return zero without changing last error, as Wine does.
inline int classic_system_metric(int index, int width, int height) {
 switch(index) {
 case SM_CXSCREEN: case SM_CXVIRTUALSCREEN: return width;
 case SM_CYSCREEN: case SM_CYVIRTUALSCREEN: return height;
 case SM_CMONITORS: case SM_SAMEDISPLAYFORMAT: return 1;
 case SM_XVIRTUALSCREEN: case SM_YVIRTUALSCREEN: return 0;
 case SM_CXBORDER: case SM_CYBORDER: return 1;
 case SM_CXDLGFRAME: case SM_CYDLGFRAME: return 3;
 case SM_CXFRAME: case SM_CYFRAME: return 4;
 case SM_CXEDGE: case SM_CYEDGE: return 2;
 case SM_CYCAPTION: return 19;
 case SM_CYSMCAPTION: return 17;
 case SM_CYMENU: return 20;
 default: return 0;
 }
}
