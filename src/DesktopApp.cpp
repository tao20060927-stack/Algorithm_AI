#include <windows.h>
#include <wrl.h>
#include <wrl/client.h>

#include <algorithm>
#include <map>
#include <string>

#include "AIPlayerEngine.h"
#include "WebView2.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
HWND g_hwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

std::wstring buildAppHtml()
{
    return std::wstring(LR"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>AI 玩家桌面端</title>
<style>
*{box-sizing:border-box}body{margin:0;font-family:"Segoe UI","Microsoft YaHei",Arial,sans-serif;background:#f3f6fa;color:#17202c}
button,select,textarea,input{font:inherit}button,select{height:34px;border:1px solid #cfd7e3;border-radius:6px;background:#fff}
button{cursor:pointer;padding:0 10px}button:hover{border-color:#2563eb;color:#2563eb}
.app{display:grid;grid-template-columns:360px 1fr;height:100vh}.side{padding:18px;background:#fff;border-right:1px solid #d7dde5;display:flex;flex-direction:column;gap:12px}
h1{font-size:24px;margin:0}p{margin:4px 0 0;color:#667085;font-size:13px}.label{display:flex;flex-direction:column;gap:7px;color:#667085;font-size:13px}
textarea{height:300px;resize:vertical;border:1px solid #d7dde5;border-radius:6px;padding:10px;font:12px/1.45 Consolas,monospace;background:#fbfcfe}
.row{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.row.five{grid-template-columns:repeat(5,1fr)}
.main{display:grid;grid-template-rows:auto minmax(0,1fr)170px;gap:14px;padding:18px;min-width:0}
.stats{display:grid;grid-template-columns:repeat(4,minmax(110px,1fr));gap:10px}.stat,.panel{background:#fff;border:1px solid #d7dde5;border-radius:8px;padding:11px}
.stat span{display:block;color:#667085;font-size:12px}.stat strong{font-size:22px;display:block;margin-top:3px}.board-wrap{background:#fff;border:1px solid #d7dde5;border-radius:8px;padding:16px;display:grid;place-items:center;overflow:auto}
#board{display:grid;gap:2px;width:min(72vh,100%);max-width:740px;aspect-ratio:1/1}.cell{display:grid;place-items:center;border-radius:3px;min-width:0;min-height:0;font-weight:700;font-size:13px;border:1px solid rgba(0,0,0,.06)}
.unknown{background:#111827;color:#111827}.wall{background:#263241}.road{background:#f8fafc}.start{background:#dbeafe;color:#1d4ed8}.exit{background:#16a34a;color:#fff}.gold{background:#f7c948}.trap{background:#e05d5d;color:#fff}.lock{background:#0f766e;color:#fff}.boss{background:#7c3aed;color:#fff}
.path{outline:2px solid rgba(37,99,235,.55);outline-offset:-2px}.visible{filter:brightness(1.08)}.player{box-shadow:inset 0 0 0 3px #111827}
.details{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;min-height:0}.panel h2{font-size:14px;margin:0 0 8px}pre{margin:0;max-height:110px;overflow:auto;white-space:pre-wrap;color:#667085;font-size:12px}
</style>
</head>
<body>
<main class="app">
  <section class="side">
    <header><h1>AI 玩家桌面端</h1><p>WebView2 本地软件，C++ 后端驱动</p></header>
    <label class="label"><span>任务 JSON</span><textarea id="jsonInput"></textarea></label>
    <div class="row"><button id="sample1">15x15 模板</button><button id="sample2">test 示例</button><button id="validate">校验</button></div>
    <label class="label"><span>算法</span><select id="algorithm"><option value="smart">完整探险 Smart</option><option value="dijkstra">完整探险 Dijkstra</option><option value="astar">完整探险 A*</option><option value="greedy">3x3 实时贪心</option></select></label>
    <div class="row five"><button id="run">运行</button><button id="pause">暂停</button><button id="prev">上一步</button><button id="step">单步</button><button id="reset">重置</button></div>
    <label class="label"><span>速度</span><input id="speed" type="range" min="80" max="1200" value="360"></label>
  </section>
  <section class="main">
    <div class="stats"><div class="stat"><span>资源</span><strong id="resource">0</strong></div><div class="stat"><span>步数</span><strong id="steps">0</strong></div><div class="stat"><span>比值</span><strong id="ratio">0.00</strong></div><div class="stat"><span>状态</span><strong id="state">待运行</strong></div></div>
    <div class="board-wrap"><div id="board"></div></div>
    <div class="details"><section class="panel"><h2>机关</h2><pre id="lockInfo">-</pre></section><section class="panel"><h2>Boss</h2><pre id="bossInfo">-</pre></section><section class="panel"><h2>事件</h2><pre id="eventInfo">-</pre></section></div>
  </section>
</main>
)HTML") + LR"HTML(
<script>
const sample1=`{"maze":[["#","#","#","#","#","#","#","#","#","#","#","S","#","#","#"],["#"," ","G","T","G","#"," "," "," "," "," "," "," "," ","#"],["#","#","#","G","#","#","#"," ","#","#","#","#","#","#","#"],["#"," ","T"," "," "," ","#"," "," "," ","#"," ","#","G","#"],["#","#","#"," ","#"," ","#","#","#"," ","#"," ","#","G","#"],["#"," ","G"," ","#"," ","#","G"," "," "," "," ","G","T","#"],["#","#","#"," ","#"," ","#","#","#","#","#","T","#","T","#"],["#"," ","#"," ","#"," ","#"," "," "," "," "," ","#"," ","#"],["#","T","#"," ","#"," ","#","#","#"," ","#","#","#"," ","#"],["#"," "," "," ","#"," "," "," "," "," "," "," ","#"," ","#"],["#"," ","#","#","#","#","#","#","#","#","#","#","#","#","#"],["#"," "," "," ","#","G","#"," ","T"," ","#","G","T"," ","#"],["#","#","#"," ","#","T","#"," ","#","#","#","#","#"," ","#"],["#"," "," "," "," "," "," "," ","B"," "," "," ","T","G","#"],["#","#","#","#","#","#","#","#","#","E","#","#","#","#","#"]],"B":[11,13,9,15],"PlayerSkills":[[8,4],[2,0],[4,2],[6,3]],"minRouds":20,"CoinConsumption":5}`;
const sample2=`{"maze":[["#","S","#","#","#","#","#","#","#","#","#"],["#"," ","#"," "," "," "," "," "," "," ","#"],["#"," ","#","#","#"," ","#"," ","#","#","#"],["#"," ","#"," "," "," ","#"," ","#","G","#"],["#","L","#"," ","#"," ","#"," ","#"," ","#"],["#"," ","#"," ","#"," ","#"," "," "," ","E"],["#","B","#"," ","#","#","#"," ","#","#","#"],["#"," "," "," "," "," ","#"," "," ","T","#"],["#"," ","#","#","#","#","#","#","#","G","#"],["#"," ","#"," "," "," ","G","T"," ","T","#"],["#","#","#","#","#","#","#","#","#","#","#"]],"B":[13,18,19,14],"PlayerSkills":[[4,1],[3,2],[5,2],[9,4],[2,0]],"C":[[2,0]],"L":"54a76d5a60849cbe4a6e7f75d830fe73f413586f329cec620eaf69bea2ade132","password":"946"}`;
const $=id=>document.getElementById(id);let maze=null,result=null,idx=0,timer=null,lastInput="",lastAlg="";
function api(){return chrome.webview.hostObjects.ai}function setState(s){$("state").textContent=s}
function tileClass(t){return t=="#"?"wall":t=="S"?"start":t=="E"?"exit":t=="G"?"gold":t=="T"?"trap":t=="L"?"lock":t=="B"?"boss":"road"}
function draw(){
if(!maze)return;
const f=result?.frames?.[idx];
const lit=new Set((f?.observed||[]).map(c=>`${c.row},${c.col}`));
const vis=new Set((f?.visible||[]).map(c=>`${c.row},${c.col}`));
const path=new Set((result?.path||[]).slice(0,idx+1).map(c=>`${c.row},${c.col}`));
$("board").style.gridTemplateColumns=`repeat(${maze[0].length},1fr)`;
$("board").innerHTML="";
maze.forEach((r,i)=>r.forEach((t,j)=>{
const d=document.createElement("div");
const k=`${i},${j}`;
const isLit=lit.has(k);
d.className=`cell ${isLit?tileClass(t):"unknown"}`;
if(isLit&&vis.has(k))d.classList.add("visible");
if(isLit&&path.has(k))d.classList.add("path");
if(f&&f.row==i&&f.col==j)d.classList.add("player");
d.textContent=f&&f.row==i&&f.col==j?"P":(isLit&&t!="#"&&t!=" "?t:"");
$("board").appendChild(d)
}));
$("resource").textContent=f?.resource??result?.resource??0;
$("steps").textContent=f?.step??result?.steps??0;
$("ratio").textContent=Number(result?.score_ratio??0).toFixed(2);
$("lockInfo").textContent=JSON.stringify(result?.lock??{},null,2);
$("bossInfo").textContent=JSON.stringify(result?.boss??{},null,2);
$("eventInfo").textContent=JSON.stringify(result?.events??[],null,2)
}
function stop(){if(timer){clearInterval(timer);timer=null}}function next(){if(!result?.frames?.length)return;idx=Math.min(idx+1,result.frames.length-1);draw();if(idx==result.frames.length-1){stop();setState(result.finished?"已抵达终点":"已停止")}}function play(){stop();timer=setInterval(next,Number($("speed").value));setState("播放中")}
async function call(name,...args){return JSON.parse(await api()[name](...args))}
async function run(){stop();const input=$("jsonInput").value,alg=$("algorithm").value;if(result?.frames?.length&&input==lastInput&&alg==lastAlg){draw();if(idx<result.frames.length-1)play();else setState(result.finished?"已抵达终点":"已停止");return}setState("运行中");const out=alg=="greedy"?await call("RunRealtimeGreedy",input):await call("RunAdventure",input,alg);if(!out.ok)throw new Error(out.error||"运行失败");maze=JSON.parse(input).maze;result=out;lastInput=input;lastAlg=alg;idx=0;draw();play()}
$("sample1").onclick=()=>{$("jsonInput").value=sample1;maze=JSON.parse(sample1).maze;result=null;idx=0;draw();setState("已加载示例")};
$("sample2").onclick=()=>{$("jsonInput").value=sample2;maze=JSON.parse(sample2).maze;result=null;idx=0;draw();setState("已加载示例")};
$("validate").onclick=async()=>{try{const out=await call("ValidateMaze",$("jsonInput").value);$("eventInfo").textContent=JSON.stringify(out,null,2);setState(out.ok?"校验通过":"校验失败")}catch(e){setState(e.message)}};
$("run").onclick=()=>run().catch(e=>{stop();setState(e.message)});$("pause").onclick=()=>{stop();setState("已暂停")};$("prev").onclick=()=>{stop();if(result?.frames?.length){idx=Math.max(idx-1,0);draw()}setState("上一步")};$("step").onclick=()=>{stop();next();setState("单步")};$("reset").onclick=()=>{stop();idx=0;draw();setState("已重置")};$("speed").oninput=()=>{if(timer)play()};
$("jsonInput").value=sample1;maze=JSON.parse(sample1).maze;draw();
</script>
</body>
</html>
)HTML";
}

std::wstring widen(const std::string &text)
{
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string narrow(const std::wstring &text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring variantString(const VARIANTARG &value)
{
    if (value.vt == VT_BSTR && value.bstrVal) return value.bstrVal;
    if ((value.vt & VT_BYREF) && (value.vt & VT_BSTR) && value.pbstrVal && *value.pbstrVal) return *value.pbstrVal;
    return {};
}

void resizeWebView()
{
    if (!g_controller) return;
    RECT bounds;
    GetClientRect(g_hwnd, &bounds);
    g_controller->put_Bounds(bounds);
}

class AiHostObject final : public IDispatch {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount_); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG count = InterlockedDecrement(&refCount_);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (!object) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDispatch) {
            *object = static_cast<IDispatch *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *count) override
    {
        if (!count) return E_POINTER;
        *count = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *names, UINT count, LCID, DISPID *ids) override
    {
        static const std::map<std::wstring, DISPID> idsByName{{L"RunRealtimeGreedy", 1},
                                                              {L"RunAdventure", 2},
                                                              {L"SolveLock", 3},
                                                              {L"RunBoss", 4},
                                                              {L"ValidateMaze", 5}};
        for (UINT i = 0; i < count; ++i) {
            const auto it = idsByName.find(names[i]);
            if (it == idsByName.end()) return DISP_E_UNKNOWNNAME;
            ids[i] = it->second;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *, UINT *) override
    {
        if (!(flags & DISPATCH_METHOD)) return DISP_E_MEMBERNOTFOUND;
        if (!params || !result) return E_POINTER;
        VariantInit(result);
        std::string output;
        if (id == 1 && params->cArgs == 1) {
            output = engine_.RunRealtimeGreedy(narrow(variantString(params->rgvarg[0])));
        } else if (id == 2 && params->cArgs == 2) {
            output = engine_.RunAdventure(narrow(variantString(params->rgvarg[1])), narrow(variantString(params->rgvarg[0])));
        } else if (id == 3 && params->cArgs == 1) {
            output = engine_.SolveLock(narrow(variantString(params->rgvarg[0])));
        } else if (id == 4 && params->cArgs == 1) {
            output = engine_.RunBoss(narrow(variantString(params->rgvarg[0])));
        } else if (id == 5 && params->cArgs == 1) {
            output = engine_.ValidateMaze(narrow(variantString(params->rgvarg[0])));
        } else {
            return DISP_E_BADPARAMCOUNT;
        }
        result->vt = VT_BSTR;
        result->bstrVal = SysAllocString(widen(output).c_str());
        return S_OK;
    }

private:
    LONG refCount_ = 1;
    AIPlayerEngine engine_;
};

void initializeWebView()
{
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
                if (FAILED(result) || !environment) return result;
                environment->CreateCoreWebView2Controller(
                    g_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT controllerResult, ICoreWebView2Controller *controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller) return controllerResult;
                            g_controller = controller;
                            g_controller->get_CoreWebView2(&g_webview);
                            resizeWebView();

                            VARIANT host;
                            VariantInit(&host);
                            host.vt = VT_DISPATCH;
                            host.pdispVal = new AiHostObject();
                            g_webview->AddHostObjectToScript(L"ai", &host);
                            host.pdispVal->Release();
                            static const std::wstring html = buildAppHtml();
                            g_webview->NavigateToString(html.c_str());
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
        case WM_SIZE:
            resizeWebView();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const wchar_t className[] = L"AIPlayerDesktopWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, className, L"AI Player Desktop", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
                             nullptr, nullptr, instance, nullptr);
    ShowWindow(g_hwnd, showCommand);
    initializeWebView();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_webview.Reset();
    g_controller.Reset();
    CoUninitialize();
    return 0;
}
