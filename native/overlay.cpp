#include "overlay.hpp"
#include <algorithm>
#include <stdexcept>
namespace hr {
void validate_overlay(const OverlaySettings& s){
    if(s.anchor<0||s.anchor>3||s.scale<75||s.scale>200||s.accent<0||s.accent>3||s.layout<0||s.layout>1||s.display<0||s.display>1||s.opacity<128||s.opacity>255||s.margin_x<0||s.margin_x>4096||s.margin_y<0||s.margin_y>4096||!s.metrics||s.metrics>63)throw std::runtime_error("Invalid HUD settings; choose at least one metric and valid layout values");
}
OverlaySettings read_overlay(const Store& store){
    OverlaySettings s;auto data=store.get("overlay.layout");if(data.empty())return s;
    try{auto rows=decode(data);if(rows.size()!=1||rows[0].size()!=11||rows[0][0]!="overlay"||rows[0][1]!="1")return s;const auto& row=rows[0];auto integer=[&](size_t i){size_t used=0;int value=std::stoi(row[i],&used);if(used!=row[i].size())throw std::runtime_error("Invalid HUD number");return value;};OverlaySettings parsed;parsed.anchor=integer(2);parsed.scale=integer(3);parsed.accent=integer(4);parsed.layout=integer(5);parsed.display=integer(6);parsed.opacity=integer(7);parsed.margin_x=integer(8);parsed.margin_y=integer(9);parsed.metrics=static_cast<unsigned>(integer(10));validate_overlay(parsed);return parsed;}catch(const std::exception&){return s;}
}
void save_overlay(Store& store,const OverlaySettings& s){validate_overlay(s);store.set("overlay.layout",encode({{"overlay","1",std::to_string(s.anchor),std::to_string(s.scale),std::to_string(s.accent),std::to_string(s.layout),std::to_string(s.display),std::to_string(s.opacity),std::to_string(s.margin_x),std::to_string(s.margin_y),std::to_string(s.metrics)}}));store.save();}
SIZE overlay_size(const OverlaySettings& s,UINT dpi){validate_overlay(s);int count=0;for(unsigned bit=1;bit<=32;bit<<=1)if(s.metrics&bit)++count;int rows=s.layout?(count+1)/2:count;return {MulDiv(s.layout?640:470,s.scale*dpi,9600),MulDiv(82+rows*28,s.scale*dpi,9600)};}
RECT overlay_bounds(const OverlaySettings& s,const RECT& work,UINT dpi){
    auto size=overlay_size(s,dpi);LONG width=std::max(1L,work.right-work.left),height=std::max(1L,work.bottom-work.top);double fit=std::min({1.0,static_cast<double>(width)/size.cx,static_cast<double>(height)/size.cy});LONG w=std::max(1L,static_cast<LONG>(size.cx*fit)),h=std::max(1L,static_cast<LONG>(size.cy*fit));LONG x=(s.anchor%2)?work.right-w-s.margin_x:work.left+s.margin_x,y=s.anchor>=2?work.bottom-h-s.margin_y:work.top+s.margin_y;x=std::clamp(x,work.left,work.left+width-w);y=std::clamp(y,work.top,work.top+height-h);return {x,y,x+w,y+h};
}
}
