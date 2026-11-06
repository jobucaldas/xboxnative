#pragma once
// Wine win32u/sysparams.c get_uint_entry and UINT_ENTRY defaults.
// This record backend has no settings store: report fixed session defaults.
// Unsupported actions, including setters, must still fail in the caller.
inline bool input_parameter_default(UINT action, UINT& value) {
 switch (action) {
 case SPI_GETKEYBOARDDELAY: value = 1; return true;
 case SPI_GETKEYBOARDSPEED: value = 31; return true;
 case SPI_GETMOUSEHOVERWIDTH: case SPI_GETMOUSEHOVERHEIGHT: value = 4; return true;
 case SPI_GETMOUSEHOVERTIME: value = 400; return true;
 case SPI_GETMOUSESPEED: value = 10; return true;
 case SPI_GETWHEELSCROLLLINES: case SPI_GETWHEELSCROLLCHARS: value = 3; return true;
 default: return false;
 }
}
