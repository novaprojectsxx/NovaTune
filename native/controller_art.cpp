#include "controller_art.hpp"
#include <algorithm>
#include <cmath>
namespace hr {
static std::vector<ControllerHotspot> draw_controller_impl(HDC dc,RECT area,bool ps,const std::set<unsigned>& pressed,COLORREF highlight){
                                                                                     
    constexpr int design_width=420,design_height=260;
    double scale=std::max(.1,std::min((area.right-area.left-12.0)/design_width,(area.bottom-area.top-12.0)/design_height));
    int left=area.left+static_cast<int>((area.right-area.left-design_width*scale)/2),top=area.top+static_cast<int>((area.bottom-area.top-design_height*scale)/2);
    auto x=[&](int value){return left+static_cast<int>(std::lround(value*scale));};auto y=[&](int value){return top+static_cast<int>(std::lround(value*scale));};
    auto point=[&](int a,int b){return POINT{x(a),y(b)};};
    int saved=SaveDC(dc);SetBkMode(dc,TRANSPARENT);
    auto font=CreateFontW(-std::max(10,static_cast<int>(13*scale)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");SelectObject(dc,font);
    auto shape=[&](COLORREF fill,COLORREF border,const auto& draw){auto brush=CreateSolidBrush(fill);auto pen=CreatePen(PS_SOLID,std::max(1,static_cast<int>(scale)),border);auto b=SelectObject(dc,brush),p=SelectObject(dc,pen);draw();SelectObject(dc,b);SelectObject(dc,p);DeleteObject(brush);DeleteObject(pen);};
    if(ps){
                                                                                         
        shape(RGB(213,216,225),RGB(242,244,250),[&]{BeginPath(dc);MoveToEx(dc,x(96),y(48),nullptr);POINT c[]={point(142,25),point(278,25),point(324,48),point(364,66),point(390,153),point(398,215),point(407,271),point(374,262),point(349,232),point(325,201),point(307,181),point(285,182),point(245,195),point(175,195),point(135,182),point(113,181),point(95,201),point(71,232),point(46,262),point(13,271),point(22,215),point(30,153),point(56,66),point(96,48)};PolyBezierTo(dc,c,24);CloseFigure(dc);EndPath(dc);StrokeAndFillPath(dc);});
        shape(RGB(24,27,34),RGB(55,61,74),[&]{BeginPath(dc);MoveToEx(dc,x(140),y(101),nullptr);POINT c[]={point(170,112),point(250,112),point(280,101),point(298,118),point(304,151),point(286,181),point(270,202),point(237,190),point(210,188),point(183,190),point(150,202),point(134,181),point(116,151),point(122,118),point(140,101)};PolyBezierTo(dc,c,15);CloseFigure(dc);EndPath(dc);StrokeAndFillPath(dc);});
                                                                                           
        auto pen=CreatePen(PS_SOLID,std::max(1,static_cast<int>(2*scale)),RGB(75,135,223));auto old=SelectObject(dc,pen);MoveToEx(dc,x(141),y(48),nullptr);LineTo(dc,x(134),y(94));LineTo(dc,x(145),y(107));MoveToEx(dc,x(279),y(48),nullptr);LineTo(dc,x(286),y(94));LineTo(dc,x(275),y(107));SelectObject(dc,old);DeleteObject(pen);
    }else{
        shape(RGB(37,40,46),RGB(108,116,130),[&]{BeginPath(dc);MoveToEx(dc,x(80),y(46),nullptr);POINT c[]={point(130,28),point(290,28),point(340,46),point(370,65),point(390,125),point(400,199),point(413,258),point(384,267),point(359,239),point(327,205),point(310,196),point(282,204),point(247,225),point(173,225),point(138,204),point(110,196),point(93,205),point(61,239),point(36,267),point(7,258),point(20,199),point(30,125),point(50,65),point(80,46)};PolyBezierTo(dc,c,24);CloseFigure(dc);EndPath(dc);StrokeAndFillPath(dc);});
                                                                         
        shape(RGB(29,32,38),RGB(54,60,70),[&]{BeginPath(dc);MoveToEx(dc,x(47),y(155),nullptr);POINT c[]={point(40,180),point(30,224),point(37,241),point(52,244),point(84,198),point(110,180),point(89,176),point(66,164),point(47,155)};PolyBezierTo(dc,c,9);CloseFigure(dc);EndPath(dc);StrokeAndFillPath(dc);});
        shape(RGB(29,32,38),RGB(54,60,70),[&]{BeginPath(dc);MoveToEx(dc,x(373),y(155),nullptr);POINT c[]={point(380,180),point(390,224),point(383,241),point(368,244),point(336,198),point(310,180),point(331,176),point(354,164),point(373,155)};PolyBezierTo(dc,c,9);CloseFigure(dc);EndPath(dc);StrokeAndFillPath(dc);});
        auto seam=CreatePen(PS_SOLID,std::max(1,static_cast<int>(scale)),RGB(67,74,87));auto old=SelectObject(dc,seam);MoveToEx(dc,x(115),y(48),nullptr);POINT c[]={point(170,39),point(250,39),point(305,48)};PolyBezierTo(dc,c,3);SelectObject(dc,old);DeleteObject(seam);
        shape(RGB(55,61,70),RGB(101,110,124),[&]{POINT disc[]={point(143,123),point(177,123),point(203,149),point(203,183),point(177,209),point(143,209),point(117,183),point(117,149)};Polygon(dc,disc,8);});
    }
    std::vector<ControllerHotspot> regions;
    auto button=[&](int bx,int by,int w,int h,unsigned code,const wchar_t* caption,bool round=false){RECT r{x(bx),y(by),x(bx+w),y(by+h)};regions.push_back({r,code});if(ps&&(code==11||code==12))regions.back().rect={x(bx+14),y(by+14),x(bx+36),y(by+36)};bool down=pressed.count(code)!=0;bool pale=ps&&(code<=4||code==14);shape(down?highlight:pale?RGB(223,226,234):RGB(23,27,33),down?highlight:pale?RGB(146,153,169):RGB(78,85,98),[&]{if(!ps&&code<=3){POINT wedge[4];if(code==0){wedge[0]=point(bx+4,by);wedge[1]=point(bx+w-4,by);wedge[2]=point(bx+w,by+h);wedge[3]=point(bx,by+h);}else if(code==1){wedge[0]=point(bx,by);wedge[1]=point(bx+w,by);wedge[2]=point(bx+w-4,by+h);wedge[3]=point(bx+4,by+h);}else if(code==2){wedge[0]=point(bx,by+4);wedge[1]=point(bx+w,by);wedge[2]=point(bx+w,by+h);wedge[3]=point(bx,by+h-4);}else{wedge[0]=point(bx,by);wedge[1]=point(bx+w,by+4);wedge[2]=point(bx+w,by+h-4);wedge[3]=point(bx,by+h);}Polygon(dc,wedge,4);}else if(round)Ellipse(dc,r.left,r.top,r.right,r.bottom);else RoundRect(dc,r.left,r.top,r.right,r.bottom,std::max(3,x(6)-x(0)),std::max(3,y(6)-y(0)));});SetTextColor(dc,down?RGB(8,15,12):pale?RGB(59,65,81):RGB(237,240,247));if(!ps&&!down){if(code==12)SetTextColor(dc,RGB(108,211,88));if(code==13)SetTextColor(dc,RGB(245,103,103));if(code==14)SetTextColor(dc,RGB(94,178,255));if(code==15)SetTextColor(dc,RGB(245,211,86));}DrawTextW(dc,caption,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);};
    button(53,8,86,22,ps?7:20,ps?L"L2":L"LT");button(281,8,86,22,ps?8:21,ps?L"R2":L"RT");
    button(38,33,92,24,ps?5:8,ps?L"L1":L"LB");button(290,33,92,24,ps?6:9,ps?L"R1":L"RB");
    const int dpad_x=ps?102:160,dpad_y=ps?111:166;
    button(dpad_x-12,dpad_y-36,24,24,ps?100:0,L"↑");button(dpad_x-36,dpad_y-12,24,24,ps?103:2,L"←");button(dpad_x+12,dpad_y-12,24,24,ps?101:3,L"→");button(dpad_x-12,dpad_y+12,24,24,ps?102:1,L"↓");
    button(304,65,30,30,ps?4:15,ps?L"△":L"Y",true);button(334,95,30,30,ps?3:13,ps?L"○":L"B",true);button(274,95,30,30,ps?1:14,ps?L"□":L"X",true);button(304,125,30,30,ps?2:12,ps?L"×":L"A",true);
    if(ps){button(147,43,126,60,14,L"");button(119,57,20,26,9,L"≡");button(281,57,20,26,10,L"⋮");button(197,126,26,24,13,L"PS",true);button(129,137,50,50,11,L"L3",true);button(241,137,50,50,12,L"R3",true);
                                                                                                
        auto stick_directions=[&](int bx,int by,unsigned base){const int rects[4][4]={{14,0,36,14},{14,36,36,50},{0,14,14,36},{36,14,50,36}};const int triangles[4][6]={{25,3,21,10,29,10},{25,47,21,40,29,40},{3,25,10,21,10,29},{47,25,40,21,40,29}};for(unsigned n=0;n<4;++n){auto r=rects[n];regions.push_back({{x(bx+r[0]),y(by+r[1]),x(bx+r[2]),y(by+r[3])},base+n});auto c=pressed.count(base+n)?highlight:RGB(106,117,136);shape(c,c,[&]{auto t=triangles[n];POINT points[]={point(bx+t[0],by+t[1]),point(bx+t[2],by+t[3]),point(bx+t[4],by+t[5])};Polygon(dc,points,3);});}};stick_directions(129,137,200);stick_directions(241,137,204);
                                                                                  
        auto pen=CreatePen(PS_SOLID,std::max(1,static_cast<int>(scale)),RGB(93,101,117));auto old=SelectObject(dc,pen),brush=SelectObject(dc,GetStockObject(NULL_BRUSH));for(int cx:{154,266})Ellipse(dc,x(cx-19),y(143),x(cx+19),y(181));SelectObject(dc,brush);SelectObject(dc,old);DeleteObject(pen);
        for(int row=0;row<2;++row)for(int col=0;col<5;++col){RECT dot{x(195+col*7),y(110+row*5),x(197+col*7),y(112+row*5)};auto b=CreateSolidBrush(RGB(127,134,150));FillRect(dc,&dot,b);DeleteObject(b);}RECT mute{x(202),y(156),x(218),y(159)};auto b=CreateSolidBrush(RGB(100,106,121));FillRect(dc,&mute,b);DeleteObject(b);
    }
    else{
        button(76,84,56,56,6,L"LS",true);button(238,140,56,56,7,L"RS",true);button(172,103,24,24,5,L"▣");button(224,103,24,24,4,L"≡");
        auto ring=CreatePen(PS_SOLID,std::max(1,static_cast<int>(scale)),RGB(105,115,131));auto old=SelectObject(dc,ring),brush=SelectObject(dc,GetStockObject(NULL_BRUSH));for(auto center:{POINT{104,112},POINT{266,168}})Ellipse(dc,x(center.x-22),y(center.y-22),x(center.x+22),y(center.y+22));SelectObject(dc,brush);SelectObject(dc,old);DeleteObject(ring);
                                                                                         
        shape(RGB(223,228,235),RGB(116,128,144),[&]{Ellipse(dc,x(197),y(60),x(223),y(86));});auto mark=CreatePen(PS_SOLID,std::max(2,static_cast<int>(3*scale)),RGB(38,43,52));old=SelectObject(dc,mark);MoveToEx(dc,x(202),y(65),nullptr);LineTo(dc,x(218),y(81));MoveToEx(dc,x(218),y(65),nullptr);LineTo(dc,x(202),y(81));SelectObject(dc,old);DeleteObject(mark);
        RECT share{x(202),y(122),x(218),y(136)};shape(RGB(24,27,33),RGB(76,84,98),[&]{RoundRect(dc,share.left,share.top,share.right,share.bottom,4,4);});SetTextColor(dc,RGB(193,202,216));DrawTextW(dc,L"↑",-1,&share,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    RestoreDC(dc,saved);DeleteObject(font);return regions;
}
std::vector<ControllerHotspot> draw_controller_art(HDC dc,RECT area,bool ps,const std::set<unsigned>& pressed,COLORREF highlight){
    struct Buffer{HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;int w=0,h=0;~Buffer(){clear();}void clear(){if(dc&&old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);dc=nullptr;bitmap=nullptr;old=nullptr;}};static Buffer buffer;
    int w=area.right-area.left,h=area.bottom-area.top;if(w<=0||h<=0)return {};
    if(buffer.w!=w||buffer.h!=h||!buffer.dc){buffer.clear();buffer.dc=CreateCompatibleDC(dc);buffer.bitmap=CreateCompatibleBitmap(dc,w*2,h*2);if(!buffer.dc||!buffer.bitmap){buffer.clear();return draw_controller_impl(dc,area,ps,pressed,highlight);}buffer.old=SelectObject(buffer.dc,buffer.bitmap);buffer.w=w;buffer.h=h;}
    RECT doubled{0,0,w*2,h*2};auto background=CreateSolidBrush(GetPixel(dc,area.left,area.top));FillRect(buffer.dc,&doubled,background);DeleteObject(background);auto regions=draw_controller_impl(buffer.dc,doubled,ps,pressed,highlight);int mode=SetStretchBltMode(dc,HALFTONE);POINT origin{};SetBrushOrgEx(dc,0,0,&origin);StretchBlt(dc,area.left,area.top,w,h,buffer.dc,0,0,w*2,h*2,SRCCOPY);SetBrushOrgEx(dc,origin.x,origin.y,nullptr);SetStretchBltMode(dc,mode);
    for(auto& region:regions){region.rect={area.left+region.rect.left/2,area.top+region.rect.top/2,area.left+region.rect.right/2,area.top+region.rect.bottom/2};}return regions;
}
}
