#include "core.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
void fails(const std::function<void()>& test,const char* message){bool failed=false;try{test();}catch(const std::exception&){failed=true;}require(failed,message);}
std::string frames(const std::string& header,const std::string& row,int n=100){
    std::string out=header+"\r\n";for(int i=0;i<n;++i)out+=row+"\r\n";return out;
}
hr::Benchmark parse(const std::string& text,const std::string& process=""){
    std::istringstream stream(text);return hr::csv(stream,process);
}
}
int main(){
    try{
        hr::Records original={{"setting","key","Unicode: \xc3\xa9 \xe6\xb8\xb8\xe6\x88\x8f"},{"empty","","tabs\tline\nquote\""},{"binary",std::string("\0\xff",2)}};
        require(hr::decode(hr::encode(original))==original,"settings roundtrip");
        fails([]{hr::decode("HEADROOM 1\n");},"reject unknown version");
        fails([]{hr::decode("HEADROOM 2\n2\t61\n");},"reject truncated settings");
        fails([]{hr::decode("HEADROOM 2\n1\txx\n");},"reject invalid hex");
        fails([]{hr::unhex("f");},"reject odd hex");
        auto b=hr::calculate(std::vector<double>(100,16.6667));
        require(std::abs(b.average-60)<.01,"60 FPS average");
        require(std::abs(b.low-60)<.01,"60 FPS low");
        require(b.frames==100 && b.stutters==0,"frame count");
        auto values=std::vector<double>(99,10);values.push_back(100);
        b=hr::calculate(values);
        require(b.low==10 && b.p95==10 && b.stutters==1,"tail metrics");
        values.insert(values.end(),{0,-1,1001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()});
        require(hr::calculate(values).frames==100,"invalid intervals filtered");
        fails([]{hr::calculate(std::vector<double>(59,10));},"minimum frames");
        b=parse(frames("\xef\xbb\xbf" "Application,MsBetweenPresents","GAME.exe,10"),"game.exe");
        require(b.average==100 && b.process=="game.exe","BOM and case insensitive game");
        require(b.metric=="MsBetweenPresents","metric identified");
        require(parse(frames("\xef\xbb\xbf" "\"Application\",MsBetweenPresents","game.exe,10")).average==100,"BOM before quoted header");
        auto mixed=frames("Application,MsBetweenPresents","game.exe,10")+frames("other.exe,50","other.exe,50",60);
        fails([&]{parse(mixed);},"mixed apps rejected");
        require(parse(mixed,"game.exe").average==100,"game filter excludes other app");
        fails([]{parse(frames("MsBetweenPresents","10"),"game.exe");},"process column required");
        b=parse(frames("Application,MsBetweenPresents,Dropped","game.exe,10,0")+
            "game.exe,500,True\r\ngame.exe,nan,0\r\ngame.exe,20junk,0\r\n");
        require(b.frames==100 && b.average==100,"dropped and invalid rows");
        b=parse(frames("Application,MsBetweenDisplayChange,MsBetweenPresents","game.exe,20,10"));
        require(b.average==50 && b.metric=="MsBetweenDisplayChange","display metric preference");
        b=parse(frames("Application,MsBetweenPresents","\"game,\"\"test\"\".exe\",10"));
        require(b.process=="game,\"test\".exe","CSV escaped quotes and commas");
        b=parse(frames("Application,MsBetweenPresents","\"game\n.exe\",10"));
        require(b.frames==100,"CSV embedded newline");
        fails([]{parse("Application,MsBetweenPresents\n\"unterminated");},"unterminated CSV rejected");
        fails([]{parse(frames("Application,MsBetweenPresents","\"game.exe\"x,10"));},"malformed quoting rejected");
        fails([]{parse(frames("Application,Unknown","game.exe,10"));},"unsupported metrics rejected");
        auto multi=frames("Application,ProcessID,SwapChainAddress,MsBetweenPresents","game.exe,1,0x1,10");
        multi+="game.exe,1,0x2,10\n";
        fails([&]{parse(multi,"game.exe");},"multiple swap chains rejected");
        hr::FrameStream live("game.exe");
        require(live.consume("Application,ProcessID,SwapChainAddress,MsBetweenPresents,Dropped")==0,"live header");
        require(live.consume("game.exe,1,0x1,10,0")==10,"live interval");
        require(live.consume("game.exe,1,0x2,50,0")==0,"live isolates first swap chain");
        require(live.consume("other.exe,1,0x1,10,0")==0,"live filters game");
        require(live.consume("game.exe,1,0x1,10,1")==0,"live drops discarded frames");
        require(live.consume("game.exe,1,0x1,nan,0")==0,"live rejects nonfinite frame");
        auto hotkey=hr::shortcut("Ctrl+Alt+G");
        require(hotkey.modifiers==0x4003 && hotkey.key=='G',"hotkey modifiers");
        require(hr::shortcut("Shift+F12").key==0x7b,"function key");
        fails([]{hr::shortcut("Ctrl+Ctrl+G");},"duplicate modifier");
        fails([]{hr::shortcut("Alt+F13");},"invalid function key");
        fails([]{hr::shortcut("Ctrl+G+");},"trailing plus");
        fails([]{hr::shortcut("G");},"modifier required");
        std::cout<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
