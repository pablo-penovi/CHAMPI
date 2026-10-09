// DPF's description of CHAMPI. Only the standalone JACK target is built for now.
#pragma once

#define DISTRHO_PLUGIN_BRAND   "CHAMPI"
#define DISTRHO_PLUGIN_NAME    "CHAMPI" // also the JACK client name
#define DISTRHO_PLUGIN_URI     "https://github.com/pablo-penovi/CHAMPI"
#define DISTRHO_PLUGIN_CLAP_ID "io.github.pablo-penovi.champi"

#define DISTRHO_PLUGIN_BRAND_ID  Cmpi
#define DISTRHO_PLUGIN_UNIQUE_ID cTap

#define DISTRHO_PLUGIN_HAS_UI             1
#define DISTRHO_PLUGIN_IS_RT_SAFE         1
#define DISTRHO_PLUGIN_IS_SYNTH           1
#define DISTRHO_PLUGIN_NUM_INPUTS         3 // mic, line L, line R
#define DISTRHO_PLUGIN_NUM_OUTPUTS        4 // master L/R, phones L/R
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT    1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT   1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1 // the UI reads the plugin's load figures

#define DISTRHO_UI_USE_NANOVG      1
#define DISTRHO_UI_USER_RESIZABLE  1
// 3.5 px/mm: the 326 x 106 mm panel and the status line under it (see champi_ui.cpp).
#define DISTRHO_UI_DEFAULT_WIDTH   1141
#define DISTRHO_UI_DEFAULT_HEIGHT  392
