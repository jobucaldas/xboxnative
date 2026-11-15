#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <climits>
#include <cstring>
#include <string>
#include <type_traits>
#include <windows.h>

namespace play_wsprintf {
constexpr size_t output_limit=1023;

template<class C> static char ascii(C c) { return static_cast<char>(c); }
template<class C> static void append(std::basic_string<C>& out,C c) {
    if(out.size()<output_limit) out.push_back(c);
}
template<class C> static unsigned number(const C*& p) {
    unsigned n=0;
    while(ascii(*p)>='0'&&ascii(*p)<='9') {
        n=std::min(2048u,n*10u+unsigned(ascii(*p)-'0')); ++p;
    }
    return n;
}
static std::wstring to_wide(const char* s,size_t n) {
    if(!s||!n)return {};
    int len=MultiByteToWideChar(CP_ACP,0,s,static_cast<int>(std::min<size_t>(n,INT_MAX)),nullptr,0);
    if(len<=0)return {};
    std::wstring out(static_cast<size_t>(len),L'\0');
    MultiByteToWideChar(CP_ACP,0,s,static_cast<int>(std::min<size_t>(n,INT_MAX)),out.data(),len);
    return out;
}
static std::string to_ansi(const wchar_t* s,size_t n) {
    if(!s||!n)return {};
    int len=WideCharToMultiByte(CP_ACP,0,s,static_cast<int>(std::min<size_t>(n,INT_MAX)),nullptr,0,nullptr,nullptr);
    if(len<=0)return {};
    std::string out(static_cast<size_t>(len),'\0');
    WideCharToMultiByte(CP_ACP,0,s,static_cast<int>(std::min<size_t>(n,INT_MAX)),out.data(),len,nullptr,nullptr);
    return out;
}
template<class C> static void emit_field(std::basic_string<C>& out,const std::basic_string<C>& text,
        unsigned width,bool left) {
    size_t pad=width>text.size()?width-text.size():0;
    if(!left) while(pad--&&out.size()<output_limit) out.push_back(static_cast<C>(' '));
    if(out.size()<output_limit) {
        size_t n=std::min(text.size(),output_limit-out.size()); out.append(text.data(),n);
    }
    if(left) while(width-->text.size()&&out.size()<output_limit) out.push_back(static_cast<C>(' '));
}
template<class C> static std::basic_string<C> integer_field(uint64_t value,bool negative,unsigned base,bool upper,
        unsigned width,int precision,bool left,bool zero,bool alternate) {
    char rev[65]; unsigned n=0;
    const char* digits=upper?"0123456789ABCDEF":"0123456789abcdef";
    do { rev[n++]=digits[value%base]; value/=base; } while(value&&n<sizeof rev);
    while(n<static_cast<unsigned>(std::max(precision,1))) rev[n++]='0';
    std::string prefix;
    if(negative) prefix.push_back('-');
    if(alternate&&base==16) prefix.append(upper?"0X":"0x");
    std::string body;
    while(n) body.push_back(rev[--n]);
    size_t pad=width>prefix.size()+body.size()?width-prefix.size()-body.size():0;
    std::basic_string<C> out;
    auto add=[&](char c){out.push_back(static_cast<C>(c));};
    if(!left&&!zero)while(pad--)add(' ');
    for(char c:prefix)add(c);
    if(!left&&zero)while(pad--)add('0');
    for(char c:body)add(c);
    if(left)while(pad--)add(' ');
    return out;
}
template<class C> static int format(C* dst,const C* fmt,va_list args,bool wide) {
    if(!dst||!fmt)return 0;
    std::basic_string<C> out;
    while(*fmt&&out.size()<output_limit) {
        if(ascii(*fmt)!='%') { append(out,*fmt++); continue; }
        ++fmt;
        if(ascii(*fmt)=='%') { append(out,*fmt++); continue; }
        bool left=false,alternate=false,zero=false;
        while(ascii(*fmt)=='-'||ascii(*fmt)=='#'||ascii(*fmt)=='0') {
            if(ascii(*fmt)=='-')left=true;
            if(ascii(*fmt)=='#')alternate=true;
            if(ascii(*fmt)=='0')zero=true;
            ++fmt;
        }
        unsigned width=number(fmt);
        int precision=-1;
        if(ascii(*fmt)=='.') { ++fmt; precision=static_cast<int>(number(fmt)); if(precision==0)precision=1; }
        char length=0;
        if(ascii(*fmt)=='h'||ascii(*fmt)=='l'||ascii(*fmt)=='I')length=ascii(*fmt++);
        char type=ascii(*fmt); if(*fmt)++fmt;
        if((length=='h'&&!std::strchr("cCdusS",type))||
           (length=='l'&&!std::strchr("cCdiuxXsS",type))||
           (length=='I'&&!std::strchr("xX",type)))continue;
        if(type=='c'||type=='C') {
            int ch=va_arg(args,int);
            if((length=='h'||length=='l')&&ch==0)continue;
            bool input_wide=length=='l'||(length!='h'&&((type=='c')==wide));
            std::basic_string<C> value;
            if(input_wide&&!wide) { wchar_t input=static_cast<wchar_t>(ch); auto converted=to_ansi(&input,1); value.assign(converted.begin(),converted.end()); }
            else if(!input_wide&&wide) { char input=static_cast<char>(ch); auto converted=to_wide(&input,1); value.assign(converted.begin(),converted.end()); }
            else value.push_back(static_cast<C>(ch));
            emit_field(out,value,width,left); continue;
        }
        if(type=='s'||type=='S') {
            bool is_wide=length=='l'||(length!='h'&&((type=='s')==wide));
            std::basic_string<C> value;
            if(is_wide) {
                const wchar_t* s=va_arg(args,const wchar_t*);
                if(s) {
                    size_t n=0; while(s[n]&&n<2048&&(precision<0||!std::is_same_v<C,wchar_t>||n<static_cast<size_t>(precision)))++n;
                    if constexpr(std::is_same_v<C,wchar_t>) value.assign(s,n);
                    else { auto converted=to_ansi(s,n); value.assign(converted.begin(),converted.end()); }
                }
            } else {
                const char* s=va_arg(args,const char*);
                if(s) {
                    size_t n=0; while(s[n]&&n<2048&&(precision<0||!std::is_same_v<C,char>||n<static_cast<size_t>(precision)))++n;
                    if constexpr(std::is_same_v<C,char>) value.assign(s,n);
                    else { auto converted=to_wide(s,n); value.assign(converted.begin(),converted.end()); }
                }
            }
            if(precision>=0&&value.size()>static_cast<size_t>(precision))value.resize(precision);
            emit_field(out,value,width,left); continue;
        }
        bool signed_type=type=='d'||type=='i';
        bool hex=type=='x'||type=='X'||type=='p';
        if(signed_type||type=='u'||hex) {
            uint64_t value=0; bool negative=false; unsigned base=hex?16:10;
            if(type=='p') value=reinterpret_cast<uintptr_t>(va_arg(args,void*));
            else if(signed_type) {
                int64_t signed_value=length=='h'?static_cast<short>(va_arg(args,int)):
                    length=='l'?static_cast<int32_t>(va_arg(args,long)):
                    length=='I'?va_arg(args,int64_t):va_arg(args,int);
                negative=signed_value<0; value=negative?uint64_t(-(signed_value+1))+1:uint64_t(signed_value);
            } else {
                value=length=='h'?static_cast<unsigned short>(va_arg(args,unsigned int)):
                    length=='l'?static_cast<uint32_t>(va_arg(args,unsigned long)):
                    length=='I'?va_arg(args,uint64_t):va_arg(args,unsigned int);
            }
            int digits=precision<0?1:precision;
            auto field=integer_field<C>(value,negative,base,type=='X',width,digits,left,zero&&precision<0,alternate||type=='p');
            size_t n=std::min(field.size(),output_limit-out.size()); out.append(field.data(),n); continue;
        }
        // Unknown format types are ignored, as with wsprintf's restricted grammar.
    }
    dst[out.size()]=0;
    std::copy(out.begin(),out.end(),dst);
    return static_cast<int>(out.size());
}
inline int __cdecl wvsprintfA(char* dst,const char* fmt,va_list args) { return format(dst,fmt,args,false); }
inline int __cdecl wvsprintfW(wchar_t* dst,const wchar_t* fmt,va_list args) { return format(dst,fmt,args,true); }
inline int __cdecl wsprintfA(char* dst,const char* fmt,...) {
    va_list args; va_start(args,fmt); int n=wvsprintfA(dst,fmt,args); va_end(args); return n;
}
inline int __cdecl wsprintfW(wchar_t* dst,const wchar_t* fmt,...) {
    va_list args; va_start(args,fmt); int n=wvsprintfW(dst,fmt,args); va_end(args); return n;
}
}
