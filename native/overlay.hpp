#pragma once
#include "windows.hpp"
namespace hr {
struct OverlaySettings {
    int anchor=0,scale=100,accent=0,layout=0,display=0,opacity=230,margin_x=24,margin_y=42;
    unsigned metrics=63;
};
void validate_overlay(const OverlaySettings& settings);
OverlaySettings read_overlay(const Store& store);
void save_overlay(Store& store,const OverlaySettings& settings);
SIZE overlay_size(const OverlaySettings& settings,UINT dpi);
RECT overlay_bounds(const OverlaySettings& settings,const RECT& work,UINT dpi);
}
