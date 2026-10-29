#include "overlay.h"
#include "../config/config.h"
#include "../game/visualization.h"
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>

namespace {
ImU32 Color(std::uint32_t rgb,std::uint8_t alpha=255) {
    return IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,alpha);
}
std::string Trim(const std::string& value) {
    const auto first=value.find_first_not_of(" \t\r\n");
    if (first==std::string::npos) return {};
    const auto last=value.find_last_not_of(" \t\r\n");
    return value.substr(first,last-first+1);
}
bool DistanceControl(const char* label,std::uint32_t& value,int low,int high,
                     const char* help) {
    int input=static_cast<int>(value);
    const bool changed=ImGui::SliderInt(label,&input,low,high,"%d m");
    if (changed) value=static_cast<std::uint32_t>(input);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s  Ctrl+click for an exact value.",help);
    return changed;
}
bool NumberControl(const char* label,std::uint32_t& value,int low,int high,
                   const char* help) {
    int input=static_cast<int>(value);
    const bool changed=ImGui::SliderInt(label,&input,low,high);
    if (changed) value=static_cast<std::uint32_t>(input);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",help);
    return changed;
}
bool ColorControl(const char* label,std::uint32_t& packed) {
    float color[3]={float((packed>>16)&255)/255.0f,float((packed>>8)&255)/255.0f,
                    float(packed&255)/255.0f};
    if (!ImGui::ColorEdit3(label,color,ImGuiColorEditFlags_NoAlpha)) return false;
    auto channel=[](float value) {
        return static_cast<std::uint32_t>(std::clamp(value,0.0f,1.0f)*255.0f+0.5f);
    };
    packed=(channel(color[0])<<16)|(channel(color[1])<<8)|channel(color[2]);
    return true;
}
void DrawTextShadow(ImDrawList* draw,ImVec2 at,ImU32 color,const char* value) {
    draw->AddText(ImVec2(at.x+1.0f,at.y+1.0f),IM_COL32(2,8,12,190),value);
    draw->AddText(at,color,value);
}
void DrawSmallTextShadow(ImDrawList* draw,ImFont* font,float size,ImVec2 at,
                         ImU32 color,const char* value) {
    const auto opacity=static_cast<unsigned>((color>>IM_COL32_A_SHIFT)&255u);
    const ImU32 edge=IM_COL32(0,5,7,std::min(opacity,225u));
    draw->AddText(font,size,ImVec2(at.x-0.9f,at.y),edge,value);
    draw->AddText(font,size,ImVec2(at.x+0.9f,at.y),edge,value);
    draw->AddText(font,size,ImVec2(at.x,at.y+1.0f),edge,value);
    draw->AddText(font,size,at,color,value);
}
std::string KeyLabel(int key) {
    if (key>=VK_F1 && key<=VK_F12) return "F"+std::to_string(key-VK_F1+1);
    if (key>='A' && key<='Z') return std::string(1,static_cast<char>(key));
    if (key>='0' && key<='9') return std::string(1,static_cast<char>(key));
    switch (key) {
    case VK_END:return "End";
    case VK_HOME:return "Home";
    case VK_INSERT:return "Insert";
    case VK_SPACE:return "Space";
    case VK_TAB:return "Tab";
    case VK_XBUTTON1:return "Mouse side 1";
    case VK_XBUTTON2:return "Mouse side 2";
    default:return "VK "+std::to_string(key);
    }
}
void DrawItemGlyph(ImDrawList* draw,ImVec2 center,std::uint8_t category,
                   std::uint32_t rgb,std::uint8_t alpha,float size,bool tile) {
    const float scale=size/18.0f;
    auto point=[&](float x,float y) {
        return ImVec2(center.x+x*scale,center.y+y*scale);
    };
    if (tile) {
        draw->AddRectFilled(point(-9,-9),point(9,9),
            IM_COL32(7,20,25,std::min<unsigned>(alpha,105u)),4.0f*scale);
        draw->AddRect(point(-9,-9),point(9,9),
            Color(rgb,static_cast<std::uint8_t>(std::min<unsigned>(alpha,75u))),
            4.0f*scale,0,1.0f);
    }
    for (int pass=0;pass<2;++pass) {
    const bool outline=pass==0;
    const ImU32 accent=outline?
        IM_COL32(0,5,7,std::min<unsigned>(alpha,220u)):Color(rgb,alpha);
    const float stroke=outline?std::max(2.35f,2.75f*scale):
        std::max(1.45f,1.95f*scale);
    const float fillPad=outline?0.8f:0.0f;
    switch (static_cast<visual::ItemKind>(category)) {
    case visual::ItemKind::Weapons:
        draw->AddLine(point(-6,2),point(6,-3),accent,stroke);
        draw->AddLine(point(-5,2),point(-2,5),accent,stroke);
        draw->AddLine(point(1,-1),point(2,4),accent,stroke);
        draw->AddLine(point(4,-3),point(7,-3),accent,stroke);break;
    case visual::ItemKind::Ammo:
        for (int i=-1;i<=1;++i) {
            const float x=static_cast<float>(i)*4.0f;
            draw->AddRect(point(x-1.1f,-4),point(x+1.1f,5),accent,1.0f*scale,0,stroke);
            draw->AddLine(point(x-1.1f,2),point(x+1.1f,2),accent,stroke);
        } break;
    case visual::ItemKind::Medical:
        draw->AddRectFilled(point(-1.6f-fillPad,-6-fillPad),
            point(1.6f+fillPad,6+fillPad),accent,1.0f*scale);
        draw->AddRectFilled(point(-6-fillPad,-1.6f-fillPad),
            point(6+fillPad,1.6f+fillPad),accent,1.0f*scale);break;
    case visual::ItemKind::Food:
        draw->AddRect(point(-4.5f,-5.5f),point(4.5f,5.5f),accent,2.0f*scale,0,stroke);
        draw->AddLine(point(-4,0),point(4,0),accent,stroke);
        draw->AddLine(point(-3,-3.5f),point(3,-3.5f),accent,stroke);break;
    case visual::ItemKind::Tools:
        draw->AddLine(point(-5,5),point(4,-4),accent,2.3f*scale);
        draw->AddCircle(point(5,-5),2.4f*scale,accent,8,stroke);
        draw->AddCircle(point(-5,5),1.5f*scale,accent,8,stroke);break;
    case visual::ItemKind::Clothing:
        draw->AddLine(point(-3,-5),point(-6,-2),accent,stroke);
        draw->AddLine(point(-6,-2),point(-4,1),accent,stroke);
        draw->AddLine(point(-4,1),point(-3,0),accent,stroke);
        draw->AddLine(point(-3,0),point(-3,6),accent,stroke);
        draw->AddLine(point(-3,6),point(3,6),accent,stroke);
        draw->AddLine(point(3,6),point(3,0),accent,stroke);
        draw->AddLine(point(3,0),point(4,1),accent,stroke);
        draw->AddLine(point(4,1),point(6,-2),accent,stroke);
        draw->AddLine(point(6,-2),point(3,-5),accent,stroke);break;
    case visual::ItemKind::Drinks:
        draw->AddRect(point(-3.8f,-5),point(3.8f,5.5f),accent,1.4f*scale,0,stroke);
        draw->AddLine(point(-3.5f,-2.5f),point(3.5f,-2.5f),accent,stroke);
        draw->AddLine(point(-2.5f,-6),point(2.5f,-6),accent,stroke);break;
    default:
        draw->AddCircle(point(0,0),4.5f*scale,accent,8,stroke);
        draw->AddCircleFilled(point(0,0),1.2f*scale,accent);break;
    }
    }
}
void DrawDirectItemLabel(ImDrawList* draw,const ItemLabelDraw& item,
                         const Config& config,ImFont* font,float rowFont,
                         float width,float height) {
    const float textWidth=font->CalcTextSizeA(rowFont,1000,0,item.label).x;
    const float x=std::clamp(item.x+8.0f,5.0f,
        std::max(5.0f,width-textWidth-19.0f));
    const float y=std::clamp(item.y-rowFont*0.5f,5.0f,
        std::max(5.0f,height-rowFont-5.0f));
    draw->AddCircleFilled(ImVec2(item.x,item.y),1.8f,Color(item.color,195));
    draw->AddLine(ImVec2(item.x+2.0f,item.y),
        ImVec2(x,y+rowFont*0.5f),Color(item.color,80),1.0f);
    DrawItemGlyph(draw,ImVec2(x+5.5f,y+rowFont*0.5f),item.category,
        config.itemCategoryColors[item.category],item.alpha,10.5f,false);
    DrawSmallTextShadow(draw,font,rowFont,ImVec2(x+13.0f,y),
        Color(item.color,item.alpha),item.label);
}
void DrawNumberedPile(ImDrawList* draw,const OverlayFrame& frame,
                      const ItemGroupDraw& group,const Config& config,
                      std::vector<std::size_t>& numbered,ImFont* font,
                      float smallFont,float rowFont,float width,float height) {
    std::sort(numbered.begin(),numbered.end(),[&](std::size_t left,std::size_t right) {
        return frame.items[left].entity<frame.items[right].entity;
    });
    const float rowHeight=16.0f;
    const std::size_t maxRows=static_cast<std::size_t>(std::max(1.0f,
        std::floor((height-28.0f)/rowHeight)));
    const std::size_t rowsPerColumn=std::min(numbered.size(),maxRows);
    const std::size_t columns=(numbered.size()+rowsPerColumn-1)/rowsPerColumn;
    float maxTextWidth=0.0f;
    for (const auto index:numbered)
        maxTextWidth=std::max(maxTextWidth,
            font->CalcTextSizeA(rowFont,1000,0,frame.items[index].label).x);
    const float columnWidth=std::clamp(maxTextWidth+28.0f,110.0f,320.0f);
    const float listWidth=columnWidth*static_cast<float>(columns);
    const float listHeight=rowHeight*static_cast<float>(rowsPerColumn);
    float x=(group.id&1u)?group.x-listWidth-13.0f:group.x+13.0f;
    if (x<5.0f || x+listWidth>width-5.0f)
        x=(group.id&1u)?group.x+13.0f:group.x-listWidth-13.0f;
    x=std::clamp(x,5.0f,std::max(5.0f,width-listWidth-5.0f));
    const float y=std::clamp(group.y+10.0f,5.0f,
        std::max(5.0f,height-listHeight-5.0f));
    for (std::size_t index=0;index<numbered.size();++index) {
        const auto& item=frame.items[numbered[index]];
        char number[12]{};sprintf_s(number,"%u",static_cast<unsigned>(index+1));
        const auto numberSize=font->CalcTextSizeA(smallFont,1000,0,number);
        draw->AddCircle(ImVec2(item.x,item.y),6.0f,
            Color(item.color,210),10,1.25f);
        DrawSmallTextShadow(draw,font,smallFont,
            ImVec2(item.x-numberSize.x*0.5f,item.y-numberSize.y*0.5f),
            Color(item.color,230),number);
        const float rowX=x+static_cast<float>(index/rowsPerColumn)*columnWidth;
        const float rowY=y+static_cast<float>(index%rowsPerColumn)*rowHeight;
        DrawSmallTextShadow(draw,font,smallFont,ImVec2(rowX,rowY+1.0f),
            Color(item.color,230),number);
        DrawSmallTextShadow(draw,font,rowFont,ImVec2(rowX+18.0f,rowY),
            Color(item.color,item.alpha),item.label);
    }
    (void)config;
}
void DrawAdaptiveItems(ImDrawList* draw,const OverlayFrame& frame,const Config& config,
                       float width,float height,ImFont* itemFont,
                       std::vector<std::size_t>& order,
                       std::vector<std::size_t>& rows,
                       std::vector<std::size_t>& splitRows) {
    order.resize(frame.itemGroups.size());std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](std::size_t left,std::size_t right) {
        const auto& a=frame.itemGroups[left],&b=frame.itemGroups[right];
        if (a.priority!=b.priority) return a.priority>b.priority;
        return a.id<b.id;
    });
    ImFont* font=itemFont?itemFont:ImGui::GetFont();
    const float smallFont=ImGui::GetFontSize()*0.68f;
    const float rowFont=ImGui::GetFontSize()*0.82f;
    constexpr float rowHeight=16.0f;
    for (const auto groupIndex:order) {
        const auto& group=frame.itemGroups[groupIndex];
        if (group.first>frame.items.size() ||
            group.count>frame.items.size()-group.first) continue;
        std::array<std::uint32_t,visual::ItemKinds> hidden{};
        rows.clear();splitRows.clear();
        float maxNameWidth=0.0f;
        for (std::uint32_t i=0;i<group.count;++i) {
            const auto index=static_cast<std::size_t>(group.first+i);
            const auto& item=frame.items[index];
            if (item.category>=hidden.size()) continue;
            if (!item.nameVisible) ++hidden[item.category];
            else if (visual::AttachItemLabel(item.distance,config.itemSplitMeters,true))
                splitRows.push_back(index);
            else {
                rows.push_back(index);
                maxNameWidth=std::max(maxNameWidth,
                    font->CalcTextSizeA(rowFont,1000,0,item.label).x);
            }
        }
        if (splitRows.size()>1) {
            splitRows.insert(splitRows.end(),rows.begin(),rows.end());
            rows.clear();
            DrawNumberedPile(draw,frame,group,config,splitRows,font,
                smallFont,rowFont,width,height);
        } else if (splitRows.size()==1)
            DrawDirectItemLabel(draw,frame.items[splitRows.front()],
                config,font,rowFont,width,height);
        if (rows.empty()) {
            float span=2.0f;
            for (const auto count:hidden) if (count) {
                char value[16]{};
                if (count>1) sprintf_s(value,"%u",count);
                span+=16.0f+(count>1?
                    font->CalcTextSizeA(smallFont,1000,0,value).x+2.0f:0.0f);
            }
            if (span<=2.0f) continue;
            const float x=std::clamp(group.x-span*0.5f,5.0f,
                std::max(5.0f,width-span-5.0f));
            const float y=std::clamp(group.y-7.0f,5.0f,
                std::max(5.0f,height-18.0f));
            float cursor=x+1.0f;
            for (std::size_t category=0;category<hidden.size();++category) {
                if (!hidden[category]) continue;
                DrawItemGlyph(draw,ImVec2(cursor+6.5f,y+7.0f),
                    static_cast<std::uint8_t>(category),config.itemCategoryColors[category],
                    static_cast<std::uint8_t>(config.itemIconOpacity),13.0f,false);
                cursor+=14.0f;
                if (hidden[category]>1) {
                    char count[16]{};sprintf_s(count,"%u",hidden[category]);
                    DrawSmallTextShadow(draw,font,smallFont,ImVec2(cursor,y+2.0f),
                        IM_COL32(210,229,230,
                            std::min(config.itemIconOpacity,235u)),count);
                    cursor+=font->CalcTextSizeA(smallFont,1000,0,count).x+2.0f;
                }
                cursor+=2.0f;
            }
            continue;
        }
        std::sort(rows.begin(),rows.end(),[&](std::size_t left,std::size_t right) {
            const auto& a=frame.items[left],&b=frame.items[right];
            const int pa=config.itemCategoryPriority[a.category];
            const int pb=config.itemCategoryPriority[b.category];
            if (pa!=pb) return pa>pb;
            if (a.category!=b.category) return a.category<b.category;
            return a.entity<b.entity;
        });
        const std::size_t maxRows=static_cast<std::size_t>(std::max(1.0f,
            std::floor((height-28.0f)/rowHeight)));
        const std::size_t rowsPerColumn=std::min(rows.size(),maxRows);
        const std::size_t columns=(rows.size()+rowsPerColumn-1)/rowsPerColumn;
        const float columnWidth=std::clamp(maxNameWidth+19.0f,105.0f,310.0f);
        const float listWidth=columnWidth*static_cast<float>(columns);
        const float listHeight=rowHeight*static_cast<float>(rowsPerColumn);
        float x=(group.id&1u)?group.x-listWidth-12.0f:group.x+12.0f;
        if (x<5.0f || x+listWidth>width-5.0f)
            x=(group.id&1u)?group.x+12.0f:group.x-listWidth-12.0f;
        x=std::clamp(x,5.0f,std::max(5.0f,width-listWidth-5.0f));
        const float y=std::clamp(group.y+10.0f,5.0f,
            std::max(5.0f,height-listHeight-5.0f));
        draw->AddCircleFilled(ImVec2(group.x,group.y),1.5f,
            IM_COL32(181,226,225,115));
        for (std::size_t index=0;index<rows.size();++index) {
            const auto& item=frame.items[rows[index]];
            const float rowX=x+static_cast<float>(index/rowsPerColumn)*columnWidth;
            const float rowY=y+static_cast<float>(index%rowsPerColumn)*rowHeight;
            DrawItemGlyph(draw,ImVec2(rowX+5.5f,rowY+6.5f),item.category,
                config.itemCategoryColors[item.category],item.alpha,11.0f,false);
            DrawSmallTextShadow(draw,font,rowFont,ImVec2(rowX+14.0f,rowY),
                Color(item.color,item.alpha),item.label);
        }
    }
}

} // namespace

bool Overlay::Initialize(HWND window) {
    RECT bounds{}; if (!GetClientRect(window,&bounds)) return false;
    itemGroupOrder_.reserve(4096);
    itemRowOrder_.reserve(4096);
    itemSplitOrder_.reserve(4096);
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Width=static_cast<UINT>(bounds.right);
    description.BufferDesc.Height=static_cast<UINT>(bounds.bottom);
    description.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count=1;
    description.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount=2;
    description.OutputWindow=window;
    description.Windowed=TRUE;
    description.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL selected{};
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&description,
        swap_.GetAddressOf(),device_.GetAddressOf(),&selected,context_.GetAddressOf());
    if (FAILED(hr) || !CreateTarget()) return false;
    width_=description.BufferDesc.Width;height_=description.BufferDesc.Height;
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().IniFilename=nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf",17.0f);
    itemFont_=ImGui::GetIO().Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf",17.0f);
    navFont_=ImGui::GetIO().Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf",25.0f);
    ImGuiStyle& theme=ImGui::GetStyle();
    theme.WindowRounding=10.0f;theme.FrameRounding=5.0f;theme.ChildRounding=7.0f;
    theme.WindowPadding=ImVec2(18,16);theme.FramePadding=ImVec2(9,6);
    theme.ItemSpacing=ImVec2(10,9);theme.ScrollbarRounding=6.0f;
    theme.Colors[ImGuiCol_WindowBg]=ImVec4(0.045f,0.075f,0.10f,0.97f);
    theme.Colors[ImGuiCol_Header]=ImVec4(0.08f,0.23f,0.25f,0.92f);
    theme.Colors[ImGuiCol_HeaderHovered]=ImVec4(0.12f,0.34f,0.36f,1.0f);
    theme.Colors[ImGuiCol_FrameBg]=ImVec4(0.09f,0.13f,0.17f,1.0f);
    theme.Colors[ImGuiCol_FrameBgHovered]=ImVec4(0.12f,0.19f,0.22f,1.0f);
    theme.Colors[ImGuiCol_CheckMark]=ImVec4(0.32f,0.85f,0.75f,1.0f);
    theme.Colors[ImGuiCol_SliderGrab]=ImVec4(0.32f,0.85f,0.75f,1.0f);
    theme.Colors[ImGuiCol_Button]=ImVec4(0.08f,0.31f,0.34f,1.0f);
    theme.Colors[ImGuiCol_ButtonHovered]=ImVec4(0.12f,0.44f,0.46f,1.0f);
    if (!ImGui_ImplWin32_Init(window) || !ImGui_ImplDX11_Init(device_.Get(),context_.Get())) {
        Shutdown();return false;
    }
    imgui_=true;return true;
}
void Overlay::Shutdown() {
    if (imgui_) {ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();imgui_=false;}
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    itemFont_=nullptr;navFont_=nullptr;
    target_.Reset();swap_.Reset();context_.Reset();device_.Reset();width_=height_=0;
    advancedEntries_.clear();advancedReady_=false;
    pendingSave_=false;
}
bool Overlay::CreateTarget() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back;
    return SUCCEEDED(swap_->GetBuffer(0,IID_PPV_ARGS(back.GetAddressOf()))) &&
        SUCCEEDED(device_->CreateRenderTargetView(back.Get(),nullptr,target_.GetAddressOf()));
}
bool Overlay::Resize(UINT width,UINT height) {
    if (!width||!height) return false;
    if (width==width_&&height==height_) return true;
    context_->OMSetRenderTargets(0,nullptr,nullptr);
    target_.Reset();
    if (FAILED(swap_->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0))) return false;
    width_=width;height_=height;return CreateTarget();
}
void Overlay::RefreshAdvanced(const Config& config) {
    advancedEntries_.clear();
    std::ifstream input(config.path);
    std::string section,line;
    while (std::getline(input,line)) {
        const auto trimmed=Trim(line);
        if (trimmed.empty() || trimmed[0]=='#' || trimmed[0]==';') continue;
        if (trimmed.front()=='[' && trimmed.back()==']') {
            section=trimmed.substr(1,trimmed.size()-2);continue;
        }
        const auto equal=trimmed.find('=');
        if (equal==std::string::npos) continue;
        RawEntry entry{};
        entry.section=section;entry.key=Trim(trimmed.substr(0,equal));
        const auto value=Trim(trimmed.substr(equal+1));
        strncpy_s(entry.value,value.c_str(),_TRUNCATE);
        advancedEntries_.push_back(std::move(entry));
    }
    advancedReady_=true;
}
bool Overlay::Draw(const OverlayFrame& frame,bool menuOpen,Config& config,
                   bool& compactItems,bool& configChanged) {
    if (!imgui_) return false;
    if (!menuOpen) {bindingItemKey_=false;bindingCopyKey_=false;}
    configChanged=false;
    ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor=menuOpen;
    ImDrawList* draw=ImGui::GetBackgroundDrawList();
    if (config.aimAssistEnabled && config.aimShowFov && frame.cameraValid) {
        const ImVec2 center(static_cast<float>(frame.viewportWidth)*0.5f,
                            static_cast<float>(frame.viewportHeight)*0.5f);
        draw->AddCircle(center,static_cast<float>(config.aimFovPixels),
            IM_COL32(117,226,207,115),64,1.2f);
    }
    if (config.compactHud) {
        ImFont* font=navFont_?navFont_:ImGui::GetFont();
        char pair[64]{};
        const bool positionReady=frame.localPositionValid &&
            visual::FormatIzurviveCoordinates(pair,sizeof(pair),frame.localPosition);
        char coordinates[96]{};
        sprintf_s(coordinates,"iZurvive X/Y  %s",positionReady?pair:"--/--");
        DrawSmallTextShadow(draw,font,23.0f,ImVec2(18.0f,15.0f),
            IM_COL32(218,237,235,250),coordinates);
        char copyHint[64]{};
        sprintf_s(copyHint,"%s TO COPY",KeyLabel(config.coordinateCopyKey).c_str());
        const char* notice=frame.coordinateCopyNotice==1?"COPIED":
            frame.coordinateCopyNotice==2?"NO POSITION":
            frame.coordinateCopyNotice==3?"COPY FAILED":copyHint;
        const float lineWidth=font->CalcTextSizeA(23.0f,1000.0f,0.0f,coordinates).x;
        const float hintX=18.0f+lineWidth+13.0f;
        if (hintX+font->CalcTextSizeA(17.0f,1000.0f,0.0f,notice).x<
            static_cast<float>(width_)-10.0f) {
            DrawSmallTextShadow(draw,font,17.0f,
                ImVec2(hintX,20.0f),
                frame.coordinateCopyNotice==1?IM_COL32(117,226,207,245):
                    frame.coordinateCopyNotice?IM_COL32(255,174,138,245):
                    IM_COL32(169,204,201,205),notice);
        }
        char heading[48]{};
        sprintf_s(heading,"HEADING  %s",frame.cameraValid?
            visual::CardinalDirection(frame.cameraForward):"--");
        DrawSmallTextShadow(draw,font,23.0f,ImVec2(18.0f,49.0f),
            IM_COL32(117,226,207,250),heading);
    }
    constexpr const char* credit="MADE BY KOREANLANDGIANT";
    ImFont* creditFont=itemFont_?itemFont_:ImGui::GetFont();
    const float creditSize=14.5f;
    DrawSmallTextShadow(draw,creditFont,creditSize,
        ImVec2(18.0f,std::max(5.0f,static_cast<float>(height_)-creditSize-14.0f)),
        IM_COL32(153,219,204,190),credit);
    if (config.cameraHud) {
        char camera[128]{};
        sprintf_s(camera,"Camera %u  |  viewport %ux%u  |  marker stage %u",
            frame.cameraValid?1u:0u,frame.viewportWidth,frame.viewportHeight,
            frame.markerStage);
        DrawTextShadow(draw,ImVec2(18,96),IM_COL32(175,198,202,235),camera);
    }
    constexpr const char* labels[]={"P","I","A","V","items"};
    if (config.funnelHud) {
        char bones[128]{};
        sprintf_s(bones,"Bones  bound %u  near %u  drawn %u",
            frame.skeletonBound,frame.skeletonEligible,frame.skeletonSampled);
        DrawTextShadow(draw,ImVec2(18,220),IM_COL32(175,198,202,235),bones);
    }
    for (std::size_t i=0;config.funnelHud && i<frame.funnel.size();++i) {
        const auto& f=frame.funnel[i];
        char line[128]{};
        sprintf_s(line,"%s  seen %u  near %u  drawn %u  max %.0f m",labels[i],
            f.seen,f.within,f.drawn,double(f.maxObserved));
        DrawTextShadow(draw,ImVec2(18.0f,130.0f+18.0f*static_cast<float>(i)),
            IM_COL32(175,198,202,235),line);
    }
    if (config.debugReadHud) {
        char budget[160]{};
        const float ratio=frame.logicalReads?
            float(frame.reads)/float(frame.logicalReads):1.0f;
        sprintf_s(budget,"Reads %u / %u logical  |  read ratio %.2f  |  refresh %u/min",
            frame.reads,frame.logicalReads,double(ratio),frame.refreshesPerMinute);
        const float debugY=config.funnelHud?246.0f:config.cameraHud?122.0f:88.0f;
        DrawTextShadow(draw,ImVec2(18,debugY),IM_COL32(175,226,207,245),budget);
    }
    if (config.watchHealthCandidates || config.watchDeathCandidates) {
        const float x=std::max(15.0f,float(width_)-350.0f);
        const float height=config.watchHealthCandidates?270.0f:125.0f;
        const float watchTop=12.0f;
        draw->AddRectFilled(ImVec2(x,watchTop),ImVec2(x+338.0f,watchTop+height),
            IM_COL32(12,25,32,220),7.0f);
        DrawTextShadow(draw,ImVec2(x+11,watchTop+7.0f),IM_COL32(117,226,207,255),
            "CANDIDATE WATCH  |  UNVERIFIED");
        float y=watchTop+32.0f;
        if (config.watchHealthCandidates) {
            if (frame.healthWatchValid) {
                constexpr const char* keys[]={"HealthWatch1","HealthWatch2","HealthWatch3","HealthWatch4",
                    "HealthWatch5","HealthWatch6","HealthWatch7","HealthWatch8"};
                for (std::size_t i=0;i<frame.healthWatch.values.size();++i) {
                    char line[96]{};
                    if (frame.healthWatch.plausible[i])
                        sprintf_s(line,"C%zu  +%03llX  %.3f",i+1,
                            static_cast<unsigned long long>(config.Offset(keys[i])),
                            double(frame.healthWatch.values[i]));
                    else sprintf_s(line,"C%zu  +%03llX  outside float range",i+1,
                        static_cast<unsigned long long>(config.Offset(keys[i])));
                    DrawTextShadow(draw,ImVec2(x+11,y),IM_COL32(198,222,223,245),line);
                    y+=18.0f;
                }
            } else {
                DrawTextShadow(draw,ImVec2(x+11,y),IM_COL32(255,174,138,245),"C1-C8  no complete local sample");
                y+=18.0f;
            }
        }
        if (config.watchDeathCandidates) {
            for (int row=0;row<2;++row) {
                const bool valid=row==0?frame.deathLocalValid:frame.deathNearbyValid;
                const auto& sample=row==0?frame.deathLocal:frame.deathNearby;
                char line[130]{};
                if (valid) sprintf_s(line,"%s D1 %08X  D2 %08X  D3 %08X  B1 %02X",
                    row==0?"LOCAL":"NEAR ",sample.words[0],sample.words[1],
                    sample.words[2],sample.byte);
                else sprintf_s(line,"%s no complete sample",row==0?"LOCAL":"NEAR ");
                DrawTextShadow(draw,ImVec2(x+11,y),IM_COL32(198,222,223,245),line);
                y+=18.0f;
            }
        }
    }
    if (config.staticRectangle) draw->AddRect(ImVec2(width_*0.45f,height_*0.42f),
        ImVec2(width_*0.55f,height_*0.58f),IM_COL32(100,255,140,230),0,0,2.0f);
    if (frame.markerValid) {
        const ImVec2 p(frame.markerX,frame.markerY);
        draw->AddCircle(p,8.0f,IM_COL32(100,255,140,240),0,2.0f);
        draw->AddLine(ImVec2(p.x-12,p.y),ImVec2(p.x-4,p.y),IM_COL32(100,255,140,240),1.5f);
        draw->AddLine(ImVec2(p.x+4,p.y),ImVec2(p.x+12,p.y),IM_COL32(100,255,140,240),1.5f);
        draw->AddText(ImVec2(p.x+12,p.y-18),IM_COL32(100,255,140,255),"infected");
    }
    for (const auto& box:frame.boxes) {
        const auto color=Color(box.color,box.local?170:245);
        if (config.showBoxes)
            draw->AddRect(ImVec2(box.left,box.top),ImVec2(box.right,box.bottom),color,0,0,box.local?1.0f:1.5f);
        DrawTextShadow(draw,ImVec2(box.left,box.top-19.0f),color,box.label);
    }
    for (const auto& skeleton:frame.skeletons) {
        const auto rig=static_cast<std::size_t>(skeleton.kind);
        if (rig>=config.skeletonPairs.size()) continue;
        const auto color=Color(skeleton.color,230);
        for (std::size_t i=0;i<config.skeletonPairCounts[rig];++i) {
            const auto pair=config.skeletonPairs[rig][i];
            if (!skeleton.visible[pair.from] || !skeleton.visible[pair.to]) continue;
            const auto& a=skeleton.joints[pair.from];
            const auto& b=skeleton.joints[pair.to];
            draw->AddLine(ImVec2(a.x,a.y),ImVec2(b.x,b.y),color,1.6f);
        }
        if (config.rawBonePoints)
            for (std::size_t i=0;i<skeleton.joints.size();++i)
                if (skeleton.visible[i]) {
                    const auto& point=skeleton.joints[i];
                    draw->AddCircleFilled(ImVec2(point.x,point.y),2.8f,
                                          IM_COL32(255,238,110,240));
                }
    }
    if (config.itemAnchoredLists)
        DrawAdaptiveItems(draw,frame,config,static_cast<float>(width_),
            static_cast<float>(height_),itemFont_,itemGroupOrder_,itemRowOrder_,
            itemSplitOrder_);
    else {
        ImFont* font=itemFont_?itemFont_:ImGui::GetFont();
        const float itemFontSize=ImGui::GetFontSize();
        for (const auto& item:frame.items) {
        if (config.itemGrid && item.cluster>1) {
            char count[24]{};
            sprintf_s(count,"+%u",static_cast<unsigned>(item.cluster-1));
            const auto nameSize=font->CalcTextSizeA(itemFontSize,1000.0f,0.0f,item.label);
            const float countFontSize=itemFontSize*0.78f;
            const auto countSize=font->CalcTextSizeA(countFontSize,1000.0f,0.0f,count);
            const float chipX=item.x+nameSize.x+6.0f;
            const float chipY=item.y+(nameSize.y-countSize.y)*0.5f;
            draw->AddRectFilled(ImVec2(chipX,chipY),
                ImVec2(chipX+countSize.x+6.0f,chipY+countSize.y+2.0f),
                IM_COL32(9,25,28,std::min<std::uint8_t>(item.alpha,58)),3.0f);
            DrawSmallTextShadow(draw,font,itemFontSize,ImVec2(item.x,item.y),
                Color(item.color,item.alpha),item.label);
            DrawSmallTextShadow(draw,font,countFontSize,
                ImVec2(chipX+3.0f,chipY+1.0f),
                IM_COL32(211,255,235,std::min<std::uint8_t>(item.alpha,180)),count);
        } else DrawSmallTextShadow(draw,font,itemFontSize,
            ImVec2(item.x,item.y),Color(item.color,item.alpha),item.label);
        }
    }
    if (menuOpen) {
        ImGui::SetNextWindowSize(ImVec2(560,680),ImGuiCond_FirstUseEver);
        ImGui::Begin("DayZ diagnostic overlay  /  settings",nullptr);
        ImGui::TextColored(ImVec4(0.43f,0.88f,0.79f,1.0f),"VISUALIZATIONS");
        ImGui::SameLine();ImGui::TextDisabled("  build %ls",config.version.c_str());
        ImGui::Separator();
        bool changed=false;
        if (ImGui::CollapsingHeader("Entities",ImGuiTreeNodeFlags_DefaultOpen)) {
            constexpr const char* names[]={"Players","Infected","Animals","Vehicles"};
            for (std::size_t i=0;i<4;++i) {
                ImGui::PushID(static_cast<int>(i));
                changed|=ImGui::Checkbox(names[i],&config.showEntities[i]);
                ImGui::SameLine(160);
                changed|=DistanceControl("Range",config.gateMeters[i],1,3000,
                    "Sample and draw this category only inside this distance.");
                changed|=ColorControl("Color",config.entityColors[i]);
                ImGui::Separator();ImGui::PopID();
            }
            changed|=ImGui::Checkbox("Show local player",&config.showLocalPlayer);
            changed|=ImGui::Checkbox("Boxes",&config.showBoxes);
            changed|=ImGui::Checkbox("Player names",&config.showPlayerNames);
            changed|=ImGui::Checkbox("Infected names",&config.showInfectedNames);
            changed|=ImGui::Checkbox("Hide dead players",&config.hideDeadPlayers);
            changed|=ImGui::Checkbox("Held items",&config.showHeldItem);
            if (ImGui::TreeNode("Skeletons")) {
                changed|=ImGui::Checkbox("Show player and infected skeletons",&config.showSkeleton);
                ImGui::TextDisabled("Players and Infected above also control their skeletons.");
                changed|=DistanceControl("Skeleton range",config.skeletonGateMeters,1,1000,
                    "Bone reads occur only inside this gate.");
                changed|=ImGui::Checkbox("Show raw joint points",&config.rawBonePoints);
                ImGui::TreePop();
            }
        }
        if (ImGui::CollapsingHeader("Assisted targeting")) {
            changed|=ImGui::Checkbox("Enable hold-key assist",&config.aimAssistEnabled);
            changed|=ImGui::Checkbox("Target infected (assist only)",
                                     &config.aimTargetInfected);
            changed|=ImGui::Checkbox("Show FOV circle",&config.aimShowFov);
            changed|=ImGui::Checkbox("Solo calibration (own pose / fixed point)",
                                     &config.aimSoloCalibration);
            if (config.aimSoloCalibration)
                ImGui::TextDisabled("Own pose is display-only. Hold steers toward a fixed point for 3 s.");
            else if (config.aimTargetInfected)
                ImGui::TextDisabled("Targets visible players and infected; your own character is excluded.");
            else
                ImGui::TextDisabled("Targets visible players only; infected visualizations stay on.");
            ImGui::TextDisabled("Read-only game data; mouse movement uses SendInput. Fire manually.");
            ImGui::TextDisabled("Hold key: %s (virtual key %d)",
                KeyLabel(config.aimHoldKey).c_str(),config.aimHoldKey);
            int holdKey=config.aimHoldKey;
            if (ImGui::InputInt("Hold key VK",&holdKey)) {
                holdKey=std::clamp(holdKey,1,255);
                if (holdKey!=config.panelKey && holdKey!=config.endKey &&
                    holdKey!=config.itemToggleKey &&
                    holdKey!=config.coordinateCopyKey) {
                    config.aimHoldKey=holdKey;changed=true;
                }
            }
            changed|=NumberControl("FOV radius (px)",config.aimFovPixels,10,1000,
                "Radius from the center of the game render rectangle, in screen pixels.");
            changed|=DistanceControl("Assist range",config.aimGateMeters,1,
                1200,"Selected target bone reads may reach this range.");
            if (ImGui::TreeNode("Prediction")) {
                changed|=ImGui::Checkbox("Predict moving targets",&config.aimPredictionEnabled);
                ImGui::TextDisabled("Uses measured target motion and held-ammo speed. Default off.");
                changed|=DistanceControl("Prediction range",config.aimPredictionGateMeters,1,
                    static_cast<int>(config.aimGateMeters),
                    "World positions are tracked only within this range.");
                changed|=NumberControl("Velocity window (ms)",config.aimVelocityWindowMs,
                    100,300,"Shorter reacts faster; longer smooths movement.");
                if (config.aimPredictionGateMeters>config.aimGateMeters) {
                    config.aimPredictionGateMeters=config.aimGateMeters;changed=true;
                }
                ImGui::TextDisabled("Optional per-weapon fallback speeds live in dayz.ini.");
                ImGui::TreePop();
            }
            constexpr const char* bones[]={"Head","Neck","Chest / spine"};
            int bone=static_cast<int>(config.aimBone);
            if (ImGui::Combo("Target bone",&bone,bones,3)) {
                config.aimBone=static_cast<std::uint32_t>(bone);changed=true;
            }
            changed|=NumberControl("Snap",config.aimSnapStrength,1,20,
                "Higher values catch up faster. 5 matches the original response; 11-20 are the stronger range.");
            changed|=NumberControl("Stickiness",config.aimHysteresisPercent,0,400,
                "Only stabilizes near-ties inside the FOV circle (up to 1 pixel). A clearly closer target takes priority.");
            if (ImGui::TreeNode("Advanced motion limits")) {
                changed|=NumberControl("Maximum rate (px/s)",
                    config.aimRatePixelsPerSecond,10,5000,
                    "Base limit; Snap scales the effective maximum rate.");
                changed|=NumberControl("Acceleration (px/s2)",
                    config.aimAccelerationPixelsPerSecond2,10,20000,
                    "Base limit; Snap scales the effective acceleration.");
                ImGui::TreePop();
            }
            if (config.aimPredictionGateMeters>config.aimGateMeters) {
                config.aimPredictionGateMeters=config.aimGateMeters;changed=true;
            }
        }
        if (ImGui::CollapsingHeader("Items",ImGuiTreeNodeFlags_DefaultOpen)) {
            changed|=ImGui::Checkbox("Show items",&config.showItems);
            ImGui::SameLine();ImGui::TextDisabled("toggle key %d",config.itemToggleKey);
            constexpr const char* categories[]={"Weapons","Ammo","Medical","Food",
                "Tools","Clothing","Other","Drinks / water"};
            if (ImGui::SmallButton("Show all categories")) {
                config.showItemCategory.fill(true);changed=true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hide all categories")) {
                config.showItemCategory.fill(false);changed=true;
            }
            for (std::size_t i=0;i<config.showItemCategory.size();++i) {
                ImGui::PushID(static_cast<int>(i)+400);
                const auto color=config.itemCategoryColors[i];
                ImGui::ColorButton("##category",ImVec4(float((color>>16)&255)/255.0f,
                    float((color>>8)&255)/255.0f,float(color&255)/255.0f,1.0f),
                    ImGuiColorEditFlags_NoTooltip,ImVec2(13,13));
                ImGui::SameLine();
                changed|=ImGui::Checkbox(categories[i],&config.showItemCategory[i]);
                ImGui::SameLine(330.0f);
                if (ImGui::SmallButton("Solo")) {
                    config.showItemCategory.fill(false);
                    config.showItemCategory[i]=true;changed=true;
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            if (DistanceControl("Item range",config.gateMeters[4],1,3000,
                "Master read gate. Category ranges remain saved when this is reduced.")) {
                changed=true;
            }
            changed|=ImGui::Checkbox("Anchored item lists",&config.itemAnchoredLists);
            changed|=ImGui::Checkbox("Category icons beyond name range",&config.itemUseIcons);
            if (DistanceControl("Name reveal range",config.itemNameMeters,0,
                static_cast<int>(config.gateMeters[4]),
                "Set all categories at once; refine individual ranges below.")) {
                for (std::size_t i=0;i<config.itemCategoryNameMeters.size();++i)
                    config.itemCategoryNameMeters[i]=std::min(config.itemNameMeters,
                        config.itemCategoryGateMeters[i]);
                changed=true;
            }
            changed|=DistanceControl("Attach individual labels within",config.itemSplitMeters,0,
                static_cast<int>(config.gateMeters[4]),
                "Close items split from the pile; each label points to its own item.");
            changed|=NumberControl("Pile radius (cm)",config.itemGroupRadiusCm,25,500,
                "Nearby world-space items share one anchored list; each name remains a row.");
            changed|=NumberControl("Icon opacity",config.itemIconOpacity,20,255,
                "Opacity of category glyphs beyond the name reveal range.");
            changed|=ImGui::Checkbox("Reveal names only near screen center",&config.itemFocusOnly);
            if (config.itemFocusOnly)
                changed|=NumberControl("Focus radius (px)",config.itemFocusRadiusPixels,32,1000,
                    "Look toward an item pile to reveal its eligible names.");
            if (ImGui::TreeNode("Name row content and legacy labels")) {
                if (DistanceControl("Near band",config.itemNearMeters,1,
                static_cast<int>(std::max(1u,config.itemMidMeters)),
                "Name, distance, and quality when enabled.")) {
                for (std::size_t i=0;i<config.itemCategoryNearMeters.size();++i)
                    config.itemCategoryNearMeters[i]=std::min(config.itemNearMeters,
                        config.itemCategoryMidMeters[i]);
                changed=true;
            }
            if (DistanceControl("Mid band",config.itemMidMeters,
                static_cast<int>(config.itemNearMeters),
                3000,"Name-only middle range.")) {
                for (std::size_t i=0;i<config.itemCategoryMidMeters.size();++i) {
                    config.itemCategoryMidMeters[i]=std::min(config.itemMidMeters,
                        config.itemCategoryGateMeters[i]);
                    config.itemCategoryNearMeters[i]=std::min(config.itemCategoryNearMeters[i],
                        config.itemCategoryMidMeters[i]);
                }
                changed=true;
            }
            if (ImGui::Checkbox("Far name",&config.itemFarName)) {
                config.itemCategoryFarName.fill(config.itemFarName);changed=true;
            }
            changed|=NumberControl("Far opacity",config.itemFarAlpha,20,255,
                "Reduced opacity for the far label band.");
            if (ImGui::Checkbox("Near distance",&config.itemNearDistance)) {
                config.itemCategoryNearDistance.fill(config.itemNearDistance);changed=true;
            }
            if (ImGui::Checkbox("Near quality",&config.itemNearQuality)) {
                config.itemCategoryNearQuality.fill(config.itemNearQuality);changed=true;
            }
            if (!config.itemAnchoredLists) {
                changed|=ImGui::Checkbox("Legacy density filter",&config.itemGrid);
                changed|=NumberControl("Legacy spacing",config.itemCellPixels,16,256,
                    "Legacy labels suppress nearby text. Anchored lists retain every item.");
                changed|=ImGui::Checkbox("Legacy +N count",&config.itemClusterCount);
            }
            if (ImGui::Checkbox("Compact name-only labels",&compactItems)) {
                config.itemCompact=compactItems;changed=true;
            }
            ImGui::TreePop();
            }
            for (std::size_t i=0;i<config.showItemCategory.size();++i) {
                ImGui::PushID(static_cast<int>(i)+100);
                char detailTitle[64]{};sprintf_s(detailTitle,"%s details",categories[i]);
                if (ImGui::TreeNode(detailTitle)) {
                    if (DistanceControl("Range",config.itemCategoryGateMeters[i],1,
                        3000,"Category gate, capped by the master range at sampling.")) {
                        config.itemCategoryMidMeters[i]=std::min(config.itemCategoryMidMeters[i],config.itemCategoryGateMeters[i]);
                        config.itemCategoryNearMeters[i]=std::min(config.itemCategoryNearMeters[i],config.itemCategoryMidMeters[i]);
                        config.itemCategoryNameMeters[i]=std::min(config.itemCategoryNameMeters[i],config.itemCategoryGateMeters[i]);
                        changed=true;
                    }
                    changed|=DistanceControl("Name range",config.itemCategoryNameMeters[i],0,
                        static_cast<int>(config.itemCategoryGateMeters[i]),
                        "Inside this distance, show this item's individual name.");
                    changed|=ColorControl("Category color",config.itemCategoryColors[i]);
                    int priority=config.itemCategoryPriority[i];
                    if (ImGui::SliderInt("Layout priority",&priority,0,10)) {
                        config.itemCategoryPriority[i]=priority;changed=true;
                    }
                    changed|=DistanceControl("Near",config.itemCategoryNearMeters[i],1,
                        static_cast<int>(std::max(1u,config.itemCategoryMidMeters[i])),
                        "Detailed label range.");
                    changed|=DistanceControl("Mid",config.itemCategoryMidMeters[i],
                        static_cast<int>(config.itemCategoryNearMeters[i]),
                        static_cast<int>(config.itemCategoryGateMeters[i]),"Name-only range.");
                    changed|=ImGui::Checkbox("Far label",&config.itemCategoryFarName[i]);
                    changed|=ImGui::Checkbox("Distance in near band",&config.itemCategoryNearDistance[i]);
                    changed|=ImGui::Checkbox("Quality in near band",&config.itemCategoryNearQuality[i]);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            constexpr const char* grades[]={"Pristine","Worn","Damaged","Badly damaged","Ruined"};
            if (ImGui::TreeNode("Quality colors")) {
                for (std::size_t i=0;i<5;++i)
                    changed|=ColorControl(grades[i],config.itemQualityColors[i]);
                ImGui::TreePop();
            }
        }
        if (ImGui::CollapsingHeader("HUD and diagnostics")) {
            changed|=ImGui::Checkbox("Navigation HUD",&config.compactHud);
            changed|=ImGui::Checkbox("Camera details",&config.cameraHud);
            changed|=ImGui::Checkbox("Funnel counters",&config.funnelHud);
            changed|=ImGui::Checkbox("Read budget counters",&config.debugReadHud);
            changed|=ImGui::Checkbox("Projection rectangle",&config.staticRectangle);
            changed|=ImGui::Checkbox("Local health candidates (C1-C8)",&config.watchHealthCandidates);
            changed|=ImGui::Checkbox("Player death candidates (D1-D3/B1)",&config.watchDeathCandidates);
            changed|=ImGui::Checkbox("Infected marker",&config.infectedMarker);
            changed|=NumberControl("Marker scan (ms)",config.markerScanMs,100,5000,"Legacy marker scan interval.");
            changed|=DistanceControl("Marker range",config.markerMaxDistance,1,3000,"Marker read gate.");
            changed|=NumberControl("Marker entries",config.markerMaxEntities,1,512,"Maximum entries per marker table.");
            changed|=NumberControl("Marker height (cm)",config.markerHeightCm,0,300,"Height above feet.");
            changed|=NumberControl("Entity scan (ms)",config.entityScanMs,100,5000,"Cache refresh interval.");
            changed|=NumberControl("Entity scan limit",config.entityScanLimit,1,4096,"Bounded scan slots.");
        }
        if (ImGui::CollapsingHeader("Hotkeys")) {
            ImGui::TextDisabled("Windows virtual-key codes: F1 112, F2 113, F3 114, End 35.");
            int keys[]={config.panelKey,config.endKey,config.itemToggleKey,
                config.coordinateCopyKey,config.aimHoldKey};
            const char* hotkeyLabels[]={"Open panel","Exit overlay","Toggle items",
                "Copy iZurvive coordinates","Hold aim assist"};
            for (int i=0;i<5;++i) {
                ImGui::PushID(i+300);
                if (ImGui::InputInt(hotkeyLabels[i],&keys[i])) {
                    keys[i]=std::clamp(keys[i],1,255);
                    bool unique=true;
                    for (int other=0;other<5;++other)
                        if (other!=i && keys[i]==keys[other]) unique=false;
                    if (unique) {
                        if (i==0) config.panelKey=keys[i];
                        else if (i==1) config.endKey=keys[i];
                        else if (i==2) config.itemToggleKey=keys[i];
                        else if (i==3) config.coordinateCopyKey=keys[i];
                        else config.aimHoldKey=keys[i];
                        changed=true;
                    }
                }
                ImGui::PopID();
            }
            ImGui::Text("Items visibility: %s",KeyLabel(config.itemToggleKey).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(bindingItemKey_?"Press a key...":"Rebind items key"))
                {bindingItemKey_=true;bindingCopyKey_=false;}
            ImGui::Text("Copy map pair: %s",KeyLabel(config.coordinateCopyKey).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(bindingCopyKey_?"Press a key...##copy":"Rebind copy key"))
                {bindingCopyKey_=true;bindingItemKey_=false;}
            if (bindingItemKey_ || bindingCopyKey_) {
                ImGui::TextDisabled("Esc cancels. Other overlay hotkeys are reserved.");
                for (int key=VK_BACK;key<=255;++key) {
                    if (key>=VK_LBUTTON && key<=VK_XBUTTON2) continue;
                    if (key==VK_SHIFT || key==VK_CONTROL || key==VK_MENU ||
                        key==VK_LSHIFT || key==VK_RSHIFT ||
                        key==VK_LCONTROL || key==VK_RCONTROL ||
                        key==VK_LMENU || key==VK_RMENU) continue;
                    if ((GetAsyncKeyState(key)&1)==0) continue;
                    if (key==VK_ESCAPE) {bindingItemKey_=false;bindingCopyKey_=false;}
                    else if (key!=config.panelKey && key!=config.endKey &&
                             key!=config.aimHoldKey &&
                             key!=(bindingItemKey_?config.coordinateCopyKey:
                                   config.itemToggleKey)) {
                        if (bindingItemKey_) config.itemToggleKey=key;
                        else config.coordinateCopyKey=key;
                        changed=true;bindingItemKey_=false;bindingCopyKey_=false;
                    }
                    break;
                }
            }
        }
        if (changed) pendingSave_=true;
        if (ImGui::CollapsingHeader("Advanced INI keys")) {
            if (!advancedReady_) RefreshAdvanced(config);
            ImGui::TextWrapped("All existing INI keys are editable here. Target identity changes take effect on the next launch; offset and budget changes restart the scene cache immediately.");
            if (ImGui::Button("Reload key list")) RefreshAdvanced(config);
            ImGui::BeginChild("AdvancedScroll",ImVec2(0,250),true);
            std::string section;
            bool reloadAdvanced=false;
            for (auto& entry:advancedEntries_) {
                if (entry.section!=section) {
                    section=entry.section;ImGui::SeparatorText(section.c_str());
                }
                ImGui::PushID(&entry);
                ImGui::SetNextItemWidth(190);
                if (ImGui::InputText(entry.key.c_str(),entry.value,sizeof(entry.value),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    if (config.EditValue(entry.section,entry.key,entry.value)) {
                        configChanged=true;
                        compactItems=config.itemCompact;
                    } else reloadAdvanced=true;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (reloadAdvanced) RefreshAdvanced(config);
        }
        ImGui::Separator();
        ImGui::TextDisabled("F1: close panel  |  End: exit  |  changes save to dayz.ini");
        ImGui::End();
    }
    if (pendingSave_ && (!menuOpen ||
        (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()))) {
        if (config.Save()) pendingSave_=false;
    }
    ImGui::Render();
    const float clear[4]={0,0,0,0};
    ID3D11RenderTargetView* target=target_.Get();
    context_->OMSetRenderTargets(1,&target,nullptr);
    context_->ClearRenderTargetView(target,clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return SUCCEEDED(swap_->Present(1,0));
}
