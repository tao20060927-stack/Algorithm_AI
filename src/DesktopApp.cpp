#include <windows.h>
#include <wrl.h>
#include <wrl/client.h>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "AIPlayerEngine.h"
#include "GameTypes.h"
#include "WebView2.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using ai_player::Json;

namespace {
HWND g_hwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;
AIPlayerEngine g_asyncEngine;
std::mutex g_asyncEngineMutex;
constexpr UINT WM_APP_WEB_RESULT = WM_APP + 1;

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
    .row{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.row.four{grid-template-columns:repeat(4,1fr)}.row.five{grid-template-columns:repeat(5,1fr)}
.main{display:grid;grid-template-rows:auto minmax(0,1fr)170px;gap:14px;padding:18px;min-width:0}
.stats{display:grid;grid-template-columns:repeat(4,minmax(110px,1fr));gap:10px}.stat,.panel{background:#fff;border:1px solid #d7dde5;border-radius:8px;padding:11px}
.stat span{display:block;color:#667085;font-size:12px}.stat strong{font-size:22px;display:block;margin-top:3px}.board-wrap{background:#fff;border:1px solid #d7dde5;border-radius:8px;padding:16px;display:grid;place-items:center;overflow:auto}
#board{display:grid;gap:2px;width:min(72vh,100%);max-width:740px;aspect-ratio:1/1}.cell{display:grid;place-items:center;border-radius:3px;min-width:0;min-height:0;font-weight:700;font-size:13px;border:1px solid rgba(0,0,0,.06)}
.unknown{background:#111827;color:#111827}.wall{background:#263241}.road{background:#f8fafc}.start{background:#dbeafe;color:#1d4ed8}.exit{background:#16a34a;color:#fff}.gold{background:#f7c948}.trap{background:#e05d5d;color:#fff}.boss{background:#7c3aed;color:#fff}
.path{outline:2px solid rgba(37,99,235,.55);outline-offset:-2px}.visible{filter:brightness(1.08)}.player{box-shadow:inset 0 0 0 3px #111827}
.monitor{background:#fff;border:1px solid #d7dde5;border-radius:8px;padding:16px;overflow:auto;width:100%;height:100%}.hidden{display:none}.debug-grid{display:grid;grid-template-columns:repeat(6,minmax(90px,1fr));gap:8px;margin-bottom:12px}.debug-grid div{border:1px solid #e1e7ef;border-radius:6px;padding:8px;background:#fbfcfe}.debug-grid span{display:block;color:#667085;font-size:11px}.debug-grid strong{font-size:16px}.diagnosis{border:1px solid #d7dde5;border-radius:6px;padding:10px;margin-bottom:12px;background:#f8fafc;color:#17202c}.debug-table{width:100%;border-collapse:collapse;font-size:12px}.debug-table th,.debug-table td{border-bottom:1px solid #e1e7ef;padding:7px;text-align:right;white-space:nowrap}.debug-table th:first-child,.debug-table td:first-child{text-align:left}.debug-table tr.selected{background:#ecfdf3}.debug-table tr.gold-row{box-shadow:inset 3px 0 0 #f7c948}
.details{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;min-height:0}.panel h2{font-size:14px;margin:0 0 8px}pre{margin:0;max-height:110px;overflow:auto;white-space:pre-wrap;color:#667085;font-size:12px}
</style>
</head>
<body>
<main class="app">
  <section class="side">
    <header><h1>AI 玩家桌面端</h1><p>WebView2 本地软件，C++ 后端驱动</p></header>
    <label class="label"><span>任务 JSON</span><textarea id="jsonInput"></textarea></label>
    <div class="row five"><button id="sample1">15x15 模板</button><button id="sample2">test 示例</button><button id="sample3">3x3 示例</button><button id="loadJson">加载</button><button id="validate">校验</button></div>
    <label class="label"><span>算法</span><select id="algorithm"><option value="smart">完整探险 Smart</option><option value="dijkstra">Reward + Dijkstra 路由</option><option value="astar">Reward + A* 路由</option><option value="branch_bound">Reward + 分支限界路由</option><option value="divide_conquer">Reward + 分治路由</option><option value="greedy">3x3 实时贪心</option><option value="resource_pickup">3x3 资源贪心</option></select></label>
    <div class="row five"><button id="run">运行</button><button id="pause">暂停</button><button id="prev">上一步</button><button id="step">单步</button><button id="reset">重置</button></div>
    <button id="viewToggle">评分监控</button>
    <label class="label"><span>速度</span><input id="speed" type="range" min="80" max="1200" value="360"></label>
  </section>
  <section class="main">
    <div class="stats"><div class="stat"><span>资源</span><strong id="resource">0</strong></div><div class="stat"><span>步数</span><strong id="steps">0</strong></div><div class="stat"><span>比值</span><strong id="ratio">0.00</strong></div><div class="stat"><span>状态</span><strong id="state">待运行</strong></div></div>
    <div id="boardView" class="board-wrap"><div id="board"></div></div>
    <div id="monitorView" class="monitor hidden"><div id="debugSummary"></div><div id="debugDiagnosis" class="diagnosis">暂无评分数据</div><div id="debugTable"></div></div>
    <div class="details"><section class="panel"><h2>Boss</h2><pre id="bossInfo">-</pre></section><section class="panel"><h2>事件</h2><pre id="eventInfo">-</pre></section></div>
  </section>
</main>
)HTML") + LR"HTML(
<script>
const sample1=`{"maze":[["#","#","#","#","#","#","#","#","#","#","#","S","#","#","#"],["#"," ","G","T","G","#"," "," "," "," "," "," "," "," ","#"],["#","#","#","G","#","#","#"," ","#","#","#","#","#","#","#"],["#"," ","T"," "," "," ","#"," "," "," ","#"," ","#","G","#"],["#","#","#"," ","#"," ","#","#","#"," ","#"," ","#","G","#"],["#"," ","G"," ","#"," ","#","G"," "," "," "," ","G","T","#"],["#","#","#"," ","#"," ","#","#","#","#","#","T","#","T","#"],["#"," ","#"," ","#"," ","#"," "," "," "," "," ","#"," ","#"],["#","T","#"," ","#"," ","#","#","#"," ","#","#","#"," ","#"],["#"," "," "," ","#"," "," "," "," "," "," "," ","#"," ","#"],["#"," ","#","#","#","#","#","#","#","#","#","#","#","#","#"],["#"," "," "," ","#","G","#"," ","T"," ","#","G","T"," ","#"],["#","#","#"," ","#","T","#"," ","#","#","#","#","#"," ","#"],["#"," "," "," "," "," "," "," ","B"," "," "," ","T","G","#"],["#","#","#","#","#","#","#","#","#","E","#","#","#","#","#"]],"B":[11,13,9,15],"PlayerSkills":[[8,4],[2,0],[4,2],[6,3]],"minRouds":20,"CoinConsumption":5}`;
const sample2=`{"maze":[["#","S","#","#","#","#","#","#","#","#","#"],["#"," ","#"," "," "," "," "," "," "," ","#"],["#"," ","#","#","#"," ","#"," ","#","#","#"],["#"," ","#"," "," "," ","#"," ","#","G","#"],["#"," ","#"," ","#"," ","#"," ","#"," ","#"],["#"," ","#"," ","#"," ","#"," "," "," ","E"],["#","B","#"," ","#","#","#"," ","#","#","#"],["#"," "," "," "," "," ","#"," "," ","T","#"],["#"," ","#","#","#","#","#","#","#","G","#"],["#"," ","#"," "," "," ","G","T"," ","T","#"],["#","#","#","#","#","#","#","#","#","#","#"]],"B":[13,18,19,14],"PlayerSkills":[[4,1],[3,2],[5,2],[9,4],[2,0]]}`;
const sample3=`{"case_id":2,"grid":[[".","T","G"],[".","P","T"],[".","T","G"]]}`;
)HTML" + LR"HTML(
const $=id=>document.getElementById(id);let maze=null,result=null,idx=0,timer=null,mazeSignature="",monitorMode=false,requestSeq=1;const pendingRequests=new Map(),resultCache=new Map();
function setState(s){$("state").textContent=s}
function tileClass(t){return t=="#"?"wall":t=="S"||t=="P"?"start":t=="E"?"exit":t=="G"?"gold":t=="T"?"trap":t=="B"?"boss":"road"}
function fmt(v,d=2){return Number.isFinite(Number(v))?Number(v).toFixed(d):"-"}
function posText(p){return p?`(${p.row},${p.col})`:"-"}
function inBounds(r,c){return maze&&r>=0&&c>=0&&r<maze.length&&c<maze[0].length}
function cellsAround(p){const cells=[];if(!p)return cells;for(let r=p.row-1;r<=p.row+1;r++)for(let c=p.col-1;c<=p.col+1;c++)if(inBounds(r,c))cells.push({row:r,col:c,tile:maze[r][c]});return cells}
function observedSetAt(frameIndex){const seen=new Set();const path=result?.path||[];const limit=Math.min(frameIndex,path.length-1);for(let i=0;i<=limit;i++)for(const c of cellsAround(path[i]))seen.add(`${c.row},${c.col}`);return seen}
function visibleCellsAt(f){return cellsAround(f?{row:f.row,col:f.col}:null)}
function parseInputMaze(){const input=$("jsonInput").value.trim();if(!input)throw new Error("请输入任务 JSON");const data=JSON.parse(input);const grid=Array.isArray(data.maze)?data.maze:data.grid;if(!Array.isArray(grid)||!Array.isArray(grid[0]))throw new Error("JSON 缺少 maze 或 grid 二维数组");return {input,data,grid,signature:JSON.stringify(grid)}}
function applyMazeData(data,signature){const changed=signature!==mazeSignature;if(changed){resultCache.clear();result=null;idx=0}maze=Array.isArray(data.maze)?data.maze:data.grid;mazeSignature=signature;return changed}
function call(name,...args){const id=requestSeq++;setState("运行中");return new Promise((resolve,reject)=>{pendingRequests.set(id,{resolve,reject});chrome.webview.postMessage({id,name,args})})}
chrome.webview.addEventListener("message",event=>{const msg=event.data||{},pending=pendingRequests.get(msg.id);if(!pending)return;pendingRequests.delete(msg.id);if(msg.ok)pending.resolve(msg.result);else pending.reject(new Error(msg.error||"运行失败"))});
function draw(){
if(!maze)return;
const f=result?.frames?.[idx];
const preview=!result?.frames?.length||result?.mode=="resource-pickup-3x3";
const lit=preview?new Set():observedSetAt(idx);
const visible=visibleCellsAt(f);
const vis=new Set(visible.map(c=>`${c.row},${c.col}`));
const path=new Set((result?.path||[]).slice(0,idx+1).map(c=>`${c.row},${c.col}`));
$("board").style.gridTemplateColumns=`repeat(${maze[0].length},1fr)`;
$("board").style.aspectRatio=`${maze[0].length}/${maze.length}`;
$("board").innerHTML="";
maze.forEach((r,i)=>r.forEach((t,j)=>{
const d=document.createElement("div");
const k=`${i},${j}`;
const isLit=lit.has(k);
d.className=`cell ${preview||isLit?tileClass(t):"unknown"}`;
if((preview||isLit)&&vis.has(k))d.classList.add("visible");
if((preview||isLit)&&path.has(k))d.classList.add("path");
if(f&&f.row==i&&f.col==j)d.classList.add("player");
d.textContent=f&&f.row==i&&f.col==j?"P":((preview||isLit)&&t!="#"&&t!=" "?t:"");
$("board").appendChild(d)
}));
$("resource").textContent=f?.resource??result?.resource??0;
$("steps").textContent=f?.step??result?.steps??0;
$("ratio").textContent=Number(result?.score_ratio??0).toFixed(2);
$("bossInfo").textContent=JSON.stringify(result?.boss??{},null,2);
$("eventInfo").textContent=JSON.stringify(result?.greedy_rounds??result?.events??[],null,2);
drawDebug()
}
function analyzeDebug(d,f,visible){
if(!d)return "当前帧没有贪心评分数据。请确认算法选择的是 3x3 实时贪心。";
const cs=d.candidates||[],gold=cs.filter(c=>c.tile=="G"),selected=cs.find(c=>c.selected),best=[...cs].sort((a,b)=>b.score-a.score)[0];
const visibleGold=(visible||[]).some(c=>c.tile=="G");
if(visibleGold&&!gold.length)return "视野里有金币，但金币没有进入候选集：优先检查 observed/visited/walkable 或路径可达性。";
if(gold.length&&best&&best.tile!="G")return `金币进入候选集，但最高分是 ${best.tile||"空格"} ${posText(best.realTarget)}：重点看 Iproxy、qEff*len、margin 是否压过金币。`;
if(gold.length&&best?.tile=="G"&&selected&&selected.tile!="G")return "金币候选分数最高，但实际没选金币：重点检查目标保持 switchMargin 或出口/兜底逻辑。";
if(gold.length&&selected?.tile=="G")return "金币进入候选集且被选中；如果画面没走向金币，检查下一帧路径映射或播放帧。";
return cs.length?"当前没有金币候选，比较普通目标的 Iproxy、qEff 和路径长度。":"当前没有候选目标，策略会进入 fallback 或停止。";
}
function drawDebug(){
const f=result?.frames?.[idx],d=f?.debug;
$("debugSummary").innerHTML=d?`<div class="debug-grid"><div><span>决策</span><strong>${d.decision}</strong></div><div><span>alpha</span><strong>${fmt(d.alpha)}</strong></div><div><span>qEff</span><strong>${fmt(d.qEff)}</strong></div><div><span>观察率</span><strong>${fmt((d.observedRatio||0)*100,1)}%</strong></div><div><span>当前位置</span><strong>${posText(d.realCurrent)}</strong></div><div><span>选中目标</span><strong>${posText(d.selectedReal)}</strong></div></div>`:"";
$("debugDiagnosis").textContent=analyzeDebug(d,f,visibleCellsAt(f));
if(!d){$("debugTable").innerHTML="";return}
const rows=[...(d.candidates||[])].sort((a,b)=>b.score-a.score).map(c=>`<tr class="${c.selected?"selected ":""}${c.tile=="G"?"gold-row":""}"><td>${c.selected?"* ":""}${c.tile||" "}</td><td>${posText(c.realTarget)}</td><td>${fmt(c.score)}</td><td>${c.deltaR}</td><td>${fmt(c.Iproxy)}</td><td>${c.unknownComponentSum??0}</td><td>${(c.unknownComponents||[]).join("+")||"0"}</td><td>${fmt(c.tailGain)}</td><td>${fmt(c.qEff)}</td><td>${c.pathLen}</td><td>${fmt(c.marginPenalty)}</td><td>${c.projectedResource}</td></tr>`).join("");
$("debugTable").innerHTML=`<table class="debug-table"><thead><tr><th>目标</th><th>坐标</th><th>score</th><th>deltaR</th><th>Iproxy</th><th>|C|合计</th><th>|C|明细</th><th>tailUB</th><th>qEff</th><th>len</th><th>margin</th><th>projR</th></tr></thead><tbody>${rows}</tbody></table>`;
}
)HTML" + LR"HTML(
function stop(){if(timer){clearInterval(timer);timer=null}}function next(){if(!result?.frames?.length)return;idx=Math.min(idx+1,result.frames.length-1);draw();if(idx==result.frames.length-1){stop();setState(result.finished?"已抵达终点":"已停止")}}function play(){stop();timer=setInterval(next,Number($("speed").value));setState("播放中")}
function loadInputJson(){stop();const parsed=parseInputMaze();applyMazeData(parsed.data,parsed.signature);if(Array.isArray(parsed.data.grid)&&!Array.isArray(parsed.data.maze))$("algorithm").value="resource_pickup";const cached=resultCache.get($("algorithm").value);if(cached)result=cached;draw();setState(`已加载 ${maze.length}x${maze[0].length}`)}
function loadSample(text){$("jsonInput").value=text;loadInputJson();setState("已加载示例")}
async function run(){stop();const parsed=parseInputMaze();let alg=$("algorithm").value;if(Array.isArray(parsed.data.grid)&&!Array.isArray(parsed.data.maze)){alg="resource_pickup";$("algorithm").value=alg}applyMazeData(parsed.data,parsed.signature);const cached=resultCache.get(alg);if(cached?.frames?.length){result=cached;draw();if(idx<result.frames.length-1)play();else setState(result.finished?"已抵达终点":"已停止");return}setState("运行中");const requestSignature=parsed.signature;const out=alg=="resource_pickup"?await call("RunResourcePickup",parsed.input):(alg=="greedy"?await call("RunRealtimeGreedy",parsed.input):await call("RunAdventure",parsed.input,alg));if(requestSignature!==mazeSignature){setState("迷宫已变更，已忽略旧结果");return}if(!out.ok)throw new Error(out.error||"运行失败");result=out;resultCache.set(alg,out);idx=0;draw();play()}
$("sample1").onclick=()=>loadSample(sample1);
$("sample2").onclick=()=>loadSample(sample2);
$("sample3").onclick=()=>{$("algorithm").value="resource_pickup";loadSample(sample3)};
$("loadJson").onclick=()=>{try{loadInputJson()}catch(e){setState(e.message)}};
$("validate").onclick=async()=>{try{const parsed=parseInputMaze();const out=Array.isArray(parsed.data.grid)?await call("RunResourcePickup",parsed.input):await call("ValidateMaze",parsed.input);$("eventInfo").textContent=JSON.stringify(out,null,2);setState(out.ok?"校验通过":"校验失败")}catch(e){setState(e.message)}};
$("viewToggle").onclick=()=>{monitorMode=!monitorMode;$("boardView").classList.toggle("hidden",monitorMode);$("monitorView").classList.toggle("hidden",!monitorMode);$("viewToggle").textContent=monitorMode?"迷宫视图":"评分监控";draw()};
$("algorithm").onchange=()=>{stop();result=resultCache.get($("algorithm").value)||null;idx=0;draw();setState(result?"已切换到缓存结果":"已切换算法")};
$("run").onclick=()=>run().catch(e=>{stop();setState(e.message)});$("pause").onclick=()=>{stop();setState("已暂停")};$("prev").onclick=()=>{stop();if(result?.frames?.length){idx=Math.max(idx-1,0);draw()}setState("上一步")};$("step").onclick=()=>{stop();next();setState("单步")};$("reset").onclick=()=>{stop();idx=0;draw();setState("已重置")};$("speed").oninput=()=>{if(timer)play()};
$("jsonInput").value=sample1;loadInputJson();
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
                                                              {L"RunBoss", 4},
                                                              {L"ValidateMaze", 5},
                                                              {L"RunResourcePickup", 6}};
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
        } else if (id == 4 && params->cArgs == 1) {
            output = engine_.RunBoss(narrow(variantString(params->rgvarg[0])));
        } else if (id == 5 && params->cArgs == 1) {
            output = engine_.ValidateMaze(narrow(variantString(params->rgvarg[0])));
        } else if (id == 6 && params->cArgs == 1) {
            output = engine_.RunResourcePickup(narrow(variantString(params->rgvarg[0])));
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

/**
 * 功能：把后台线程生成的 JSON 响应投递回窗口线程。
 * 输入：
 *   - response：要发送给 WebView 页面的 JSON 响应。
 * 输出：
 *   - 无返回值，通过 Windows 消息异步交给 UI 线程处理。
 * 关键逻辑：
 *   - WebView2 COM 对象属于 UI STA 线程，后台算法线程不能直接调用 PostWebMessageAsJson。
 */
void postResponseToUiThread(const Json &response)
{
    auto *payload = new std::wstring(widen(response.dump()));
    PostMessageW(g_hwnd, WM_APP_WEB_RESULT, 0, reinterpret_cast<LPARAM>(payload));
}

/**
 * 功能：在后台线程执行前端请求的 AI 计算。
 * 输入：
 *   - messageJson：前端通过 chrome.webview.postMessage 发送的请求 JSON。
 * 输出：
 *   - 无返回值，计算完成后把结果异步发回前端。
 * 关键逻辑：
 *   - 后台请求共享同一个 AIPlayerEngine，让 C++ 端 resultCache_ 在窗口生命周期内持续有效。
 *   - AIPlayerEngine 内部缓存不是并发容器，因此用互斥锁串行保护算法调用。
 *   - 返回消息保留请求 id，让前端 Promise 能对应 resolve/reject。
 */
void runWebRequestAsync(std::wstring messageJson)
{
    std::thread([messageJson = std::move(messageJson)]() {
        Json response;
        int id = 0;
        try {
            const Json request = Json::parse(narrow(messageJson));
            id = request.value("id", 0);
            const std::string name = request.value("name", "");
            const Json args = request.value("args", Json::array());

            std::string output;
            std::lock_guard<std::mutex> lock(g_asyncEngineMutex);
            if (name == "RunRealtimeGreedy" && args.size() == 1) {
                output = g_asyncEngine.RunRealtimeGreedy(args[0].get<std::string>());
            } else if (name == "RunAdventure" && args.size() == 2) {
                output = g_asyncEngine.RunAdventure(args[0].get<std::string>(), args[1].get<std::string>());
            } else if (name == "RunBoss" && args.size() == 1) {
                output = g_asyncEngine.RunBoss(args[0].get<std::string>());
            } else if (name == "ValidateMaze" && args.size() == 1) {
                output = g_asyncEngine.ValidateMaze(args[0].get<std::string>());
            } else if (name == "RunResourcePickup" && args.size() == 1) {
                output = g_asyncEngine.RunResourcePickup(args[0].get<std::string>());
            } else {
                throw std::runtime_error("unsupported async request");
            }
            response = Json{{"id", id}, {"ok", true}, {"result", Json::parse(output)}};
        } catch (const std::exception &ex) {
            response = Json{{"id", id}, {"ok", false}, {"error", ex.what()}};
        }
        postResponseToUiThread(response);
    }).detach();
}

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
                             EventRegistrationToken messageToken{};
                             g_webview->add_WebMessageReceived(
                                 Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                     [](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                                         LPWSTR rawMessage = nullptr;
                                         args->get_WebMessageAsJson(&rawMessage);
                                         std::wstring message = rawMessage ? rawMessage : L"";
                                         CoTaskMemFree(rawMessage);
                                         runWebRequestAsync(std::move(message));
                                         return S_OK;
                                     })
                                     .Get(),
                                 &messageToken);
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
        case WM_APP_WEB_RESULT: {
            std::unique_ptr<std::wstring> payload(reinterpret_cast<std::wstring *>(lparam));
            if (g_webview && payload) {
                g_webview->PostWebMessageAsJson(payload->c_str());
            }
            return 0;
        }
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
