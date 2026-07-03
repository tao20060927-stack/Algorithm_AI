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
button{cursor:pointer;padding:0 10px}button:hover,button.active{border-color:#2563eb;color:#2563eb}
.app{display:grid;grid-template-columns:360px 1fr;height:100vh}.side{padding:18px;background:#fff;border-right:1px solid #d7dde5;display:flex;flex-direction:column;gap:12px}
h1{font-size:24px;margin:0}p{margin:4px 0 0;color:#667085;font-size:13px}.label{display:flex;flex-direction:column;gap:7px;color:#667085;font-size:13px}
textarea{height:300px;resize:vertical;border:1px solid #d7dde5;border-radius:6px;padding:10px;font:12px/1.45 Consolas,monospace;background:#fbfcfe}
    .row{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.row.four{grid-template-columns:repeat(4,1fr)}.row.five{grid-template-columns:repeat(5,1fr)}
.main{display:grid;grid-template-rows:auto minmax(0,1fr);gap:14px;padding:18px;min-width:0}
.stats{display:grid;grid-template-columns:repeat(4,minmax(110px,1fr));gap:10px}.stat,.panel{background:#fff;border:1px solid #d7dde5;border-radius:8px;padding:11px}
.stat span{display:block;color:#667085;font-size:12px}.stat strong{font-size:22px;display:block;margin-top:3px}.board-wrap{background:#fff;border:1px solid #d7dde5;border-radius:8px;padding:16px;display:grid;place-items:center;overflow:auto}
#board{display:grid;gap:2px;max-width:740px}.cell{display:grid;place-items:center;border-radius:3px;min-width:0;min-height:0;width:100%;height:100%;font-weight:700;font-size:13px;line-height:1;border:1px solid rgba(0,0,0,.06)}
.unknown{background:#111827;color:#111827}.wall{background:#263241}.road{background:#f8fafc}.start{background:#dbeafe;color:#1d4ed8}.exit{background:#16a34a;color:#fff}.gold{background:#f7c948}.trap{background:#e05d5d;color:#fff}.boss{background:#7c3aed;color:#fff}
.path{outline:2px solid rgba(37,99,235,.55);outline-offset:-2px}.visible{filter:brightness(1.08)}.player{box-shadow:inset 0 0 0 3px #111827}
.monitor{background:#fff;border:1px solid #d7dde5;border-radius:8px;padding:16px;overflow:auto;width:100%;height:100%}.hidden{display:none}.debug-grid{display:grid;grid-template-columns:repeat(6,minmax(90px,1fr));gap:8px;margin-bottom:12px}.debug-grid div{border:1px solid #e1e7ef;border-radius:6px;padding:8px;background:#fbfcfe}.debug-grid span{display:block;color:#667085;font-size:11px}.debug-grid strong{font-size:16px}.diagnosis{border:1px solid #d7dde5;border-radius:6px;padding:10px;margin-bottom:12px;background:#f8fafc;color:#17202c}.debug-table{width:100%;border-collapse:collapse;font-size:12px}.debug-table th,.debug-table td{border-bottom:1px solid #e1e7ef;padding:7px;text-align:right;white-space:nowrap}.debug-table th:first-child,.debug-table td:first-child{text-align:left}.debug-table tr.selected{background:#ecfdf3}.debug-table tr.gold-row{box-shadow:inset 3px 0 0 #f7c948}
.boss-section{border:1px solid #e1e7ef;border-radius:6px;padding:10px;margin-top:10px;background:#fbfcfe}.boss-section h3{font-size:14px;margin:0 0 8px}.event-badge{display:inline-block;border-radius:999px;padding:2px 8px;background:#eef4ff;color:#2563eb}.sequence-pill{display:inline-block;border:1px solid #d7dde5;border-radius:999px;padding:2px 8px;background:#fff;white-space:nowrap}
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
    <div class="row"><button id="viewBoard" class="active">迷宫视图</button><button id="viewScore">评分监控</button><button id="viewBoss">Boss事件</button></div>
    <label class="label"><span>速度</span><input id="speed" type="range" min="80" max="1200" value="360"></label>
  </section>
  <section class="main">
    <div class="stats"><div class="stat"><span>资源</span><strong id="resource">0</strong></div><div class="stat"><span>步数</span><strong id="steps">0</strong></div><div class="stat"><span>比值</span><strong id="ratio">0.00</strong></div><div class="stat"><span>状态</span><strong id="state">待运行</strong></div></div>
    <div id="boardView" class="board-wrap"><div id="board"></div></div>
    <div id="monitorView" class="monitor hidden"><div id="debugSummary"></div><div id="debugDiagnosis" class="diagnosis">暂无评分数据</div><div id="debugTable"></div></div>
    <div id="bossEventView" class="monitor hidden"><div id="bossEventSummary"></div><div id="bossEventTimeline"></div><div id="bossEventTable"></div></div>
  </section>
</main>
)HTML") + LR"HTML(
<script>
const sample1=`{"maze":[["#","#","#","#","#","#","#","#","#","#","#","S","#","#","#"],["#"," ","G","T","G","#"," "," "," "," "," "," "," "," ","#"],["#","#","#","G","#","#","#"," ","#","#","#","#","#","#","#"],["#"," ","T"," "," "," ","#"," "," "," ","#"," ","#","G","#"],["#","#","#"," ","#"," ","#","#","#"," ","#"," ","#","G","#"],["#"," ","G"," ","#"," ","#","G"," "," "," "," ","G","T","#"],["#","#","#"," ","#"," ","#","#","#","#","#","T","#","T","#"],["#"," ","#"," ","#"," ","#"," "," "," "," "," ","#"," ","#"],["#","T","#"," ","#"," ","#","#","#"," ","#","#","#"," ","#"],["#"," "," "," ","#"," "," "," "," "," "," "," ","#"," ","#"],["#"," ","#","#","#","#","#","#","#","#","#","#","#","#","#"],["#"," "," "," ","#","G","#"," ","T"," ","#","G","T"," ","#"],["#","#","#"," ","#","T","#"," ","#","#","#","#","#"," ","#"],["#"," "," "," "," "," "," "," ","B"," "," "," ","T","G","#"],["#","#","#","#","#","#","#","#","#","E","#","#","#","#","#"]],"B":[11,13,9,15],"PlayerSkills":[[8,4],[2,0],[4,2],[6,3]],"minRouds":20,"CoinConsumption":5}`;
const sample2=`{"maze":[["#","S","#","#","#","#","#","#","#","#","#"],["#"," ","#"," "," "," "," "," "," "," ","#"],["#"," ","#","#","#"," ","#"," ","#","#","#"],["#"," ","#"," "," "," ","#"," ","#","G","#"],["#"," ","#"," ","#"," ","#"," ","#"," ","#"],["#"," ","#"," ","#"," ","#"," "," "," ","E"],["#","B","#"," ","#","#","#"," ","#","#","#"],["#"," "," "," "," "," ","#"," "," ","T","#"],["#"," ","#","#","#","#","#","#","#","G","#"],["#"," ","#"," "," "," ","G","T"," ","T","#"],["#","#","#","#","#","#","#","#","#","#","#"]],"B":[13,18,19,14],"PlayerSkills":[[4,1],[3,2],[5,2],[9,4],[2,0]]}`;
const sample3=`{"case_id":2,"grid":[[".","T","G"],[".","P","T"],[".","T","G"]]}`;
)HTML" + LR"HTML(
const $=id=>document.getElementById(id);let maze=null,result=null,idx=0,timer=null,mazeSignature="",viewMode="board",requestSeq=1;const pendingRequests=new Map(),resultCache=new Map();
const renderState={boardKey:"",cells:[],result:null,preview:null,lit:new Set(),litLimit:-1,path:new Set(),pathLimit:-1,bossEventResult:undefined};
function setState(s){$("state").textContent=s}
function tileClass(t){return t=="#"?"wall":t=="S"||t=="P"?"start":t=="E"?"exit":t=="G"?"gold":t=="T"?"trap":t=="B"?"boss":"road"}
function fmt(v,d=2){return Number.isFinite(Number(v))?Number(v).toFixed(d):"-"}
function posText(p){return p?`(${p.row},${p.col})`:"-"}
function inBounds(r,c){return maze&&r>=0&&c>=0&&r<maze.length&&c<maze[0].length}
function cellsAround(p){const cells=[];if(!p)return cells;for(let r=p.row-1;r<=p.row+1;r++)for(let c=p.col-1;c<=p.col+1;c++)if(inBounds(r,c))cells.push({row:r,col:c,tile:maze[r][c]});return cells}
function visibleCellsAt(f){return cellsAround(f?{row:f.row,col:f.col}:null)}
function parseInputMaze(){const input=$("jsonInput").value.trim();if(!input)throw new Error("请输入任务 JSON");const data=JSON.parse(input);const grid=Array.isArray(data.maze)?data.maze:data.grid;if(!Array.isArray(grid)||!Array.isArray(grid[0]))throw new Error("JSON 缺少 maze 或 grid 二维数组");return {input,data,grid,signature:JSON.stringify(grid)}}
// 重置逐帧前缀缓存，避免切换算法、重置或回退时复用旧路径状态。
function resetFrameCaches(){renderState.result=null;renderState.preview=null;renderState.lit=new Set();renderState.litLimit=-1;renderState.path=new Set();renderState.pathLimit=-1}
function applyMazeData(data,signature){const changed=signature!==mazeSignature;if(changed){resultCache.clear();result=null;idx=0;renderState.boardKey="";renderState.cells=[];renderState.bossEventResult=undefined;resetFrameCaches()}maze=Array.isArray(data.maze)?data.maze:data.grid;mazeSignature=signature;return changed}
function call(name,...args){const id=requestSeq++;setState("运行中");return new Promise((resolve,reject)=>{pendingRequests.set(id,{resolve,reject});chrome.webview.postMessage({id,name,args})})}
chrome.webview.addEventListener("message",event=>{const msg=event.data||{},pending=pendingRequests.get(msg.id);if(!pending)return;pendingRequests.delete(msg.id);if(msg.ok)pending.resolve(msg.result);else pending.reject(new Error(msg.error||"运行失败"))});
// 棋盘按整数物理像素计算格子尺寸，避免 1fr 和 Windows 缩放产生的小数像素被浏览器分摊。
function layoutBoard(){
if(!maze)return;
const board=$("board"),wrap=$("boardView"),rows=maze.length,cols=maze[0].length,maxBoard=740,dpr=window.devicePixelRatio||1;
const style=getComputedStyle(wrap);
const wrapW=wrap.clientWidth-parseFloat(style.paddingLeft)-parseFloat(style.paddingRight);
const wrapH=wrap.clientHeight-parseFloat(style.paddingTop)-parseFloat(style.paddingBottom);
const fallback=Math.min(window.innerHeight*.72,maxBoard);
const availableW=Math.min(wrapW>0?wrapW:fallback,maxBoard);
const availableH=Math.min(wrapH>0?wrapH:fallback,maxBoard);
const gapPx=Math.max(1,Math.round(2*dpr));
const availWPx=Math.floor(availableW*dpr),availHPx=Math.floor(availableH*dpr);
const cellPx=Math.max(8,Math.floor(Math.min((availWPx-gapPx*(cols-1))/cols,(availHPx-gapPx*(rows-1))/rows)));
const cellSize=cellPx/dpr,gap=gapPx/dpr;
board.style.gap=`${gap}px`;
board.style.gridTemplateColumns=`repeat(${cols},${cellSize}px)`;
board.style.gridTemplateRows=`repeat(${rows},${cellSize}px)`;
board.style.width=`${(cols*cellPx+gapPx*(cols-1))/dpr}px`;
board.style.height=`${(rows*cellPx+gapPx*(rows-1))/dpr}px`
}
// 棋盘格只在迷宫变化时创建；播放过程中只更新已有格子的 class 和文字。
function ensureBoard(){
if(!maze)return;
const rows=maze.length,cols=maze[0].length,key=`${mazeSignature}:${rows}x${cols}`;
if(renderState.boardKey==key&&renderState.cells.length==rows*cols){layoutBoard();return}
const board=$("board"),fragment=document.createDocumentFragment();
renderState.cells=[];
for(let i=0;i<rows;i++)for(let j=0;j<cols;j++){const d=document.createElement("div");d.className="cell unknown";fragment.appendChild(d);renderState.cells.push(d)}
board.replaceChildren(fragment);
layoutBoard();
renderState.boardKey=key
}
window.addEventListener("resize",layoutBoard);
// observed/path 都按播放方向增量扩展；回退或切换结果时才从当前帧重新累计。
function ensureFrameCaches(preview,frameIndex){
const path=result?.path||[];
const limit=Math.min(frameIndex,path.length-1);
if(renderState.result!==result||renderState.preview!==preview||frameIndex<renderState.litLimit||frameIndex<renderState.pathLimit){resetFrameCaches();renderState.result=result;renderState.preview=preview}
if(!preview){for(let i=renderState.litLimit+1;i<=limit;i++)for(const c of cellsAround(path[i]))renderState.lit.add(`${c.row},${c.col}`);renderState.litLimit=limit}
for(let i=renderState.pathLimit+1;i<=limit;i++)renderState.path.add(`${path[i].row},${path[i].col}`);
renderState.pathLimit=limit
}
)HTML" + LR"HTML(
function esc(v){return String(v??"-").replace(/[&<>"]/g,s=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;"}[s]))}
function seqText(seq){return Array.isArray(seq)&&seq.length?seq.map(x=>x<0?"等待":`技能${x}`).join(" -> "):"-"}
function sameSeq(a,b){return Array.isArray(a)&&Array.isArray(b)&&a.length==b.length&&a.every((v,i)=>v==b[i])}
function currentBossResult(){return result?.boss||(result?.events||[]).find(e=>e.result)?.result||null}
function bossEvents(){return (result?.events||[]).filter(e=>String(e.type||"").startsWith("boss"))}
function bossEventName(t){return t=="boss"?"Boss胜利":t=="boss_revive"?"Boss失败复活":t=="boss_game_over"?"Boss失败结束":t||"-"}
function drawBossEvents(){
if(renderState.bossEventResult===result)return;
renderState.bossEventResult=result;
const boss=currentBossResult(),events=bossEvents(),attempts=boss?.attempts||[],attempt=attempts.length?attempts[attempts.length-1]:null,phases=boss?.phases||attempt?.phases||[];
if(!boss&&!events.length){$("bossEventSummary").innerHTML="";$("bossEventTimeline").innerHTML='<div class="diagnosis">暂无 Boss 事件。运行包含 Boss 的迷宫后，这里会展示触发事件和技能选择。</div>';$("bossEventTable").innerHTML="";return}
$("bossEventSummary").innerHTML=`<div class="debug-grid"><div><span>Boss结果</span><strong>${boss?.ok?"成功":"失败"}</strong></div><div><span>算法</span><strong>${esc(boss?.algorithm||"-")}</strong></div><div><span>回合</span><strong>${boss?.turns??"-"}/${boss?.minRounds??"-"}</strong></div><div><span>限制内</span><strong>${boss?.withinMinRounds===false?"否":"是"}</strong></div><div><span>复活金币</span><strong>${boss?.CoinConsumption??"-"}</strong></div><div><span>阶段数</span><strong>${phases.length}</strong></div></div>`;
const eventRows=events.map(e=>`<tr><td>${e.step??"-"}</td><td><span class="event-badge">${esc(bossEventName(e.type))}</span></td><td>${e.reviveCost??""}</td><td>${e.result?.turns??""}</td><td>${e.result?.withinMinRounds===false?"否":"是"}</td></tr>`).join("");
$("bossEventTimeline").innerHTML=`<section class="boss-section"><h3>触发事件</h3><table class="debug-table"><thead><tr><th>步数</th><th>事件</th><th>复活消耗</th><th>Boss回合</th><th>限制内</th></tr></thead><tbody>${eventRows||'<tr><td colspan="5">暂无触发事件</td></tr>'}</tbody></table></section>`;
const blocks=phases.map(p=>{const rows=[...(p.candidateScores||[])].slice(0,8).map(c=>`<tr class="${sameSeq(c.sequence,p.sequence)?"selected":""}"><td>${sameSeq(c.sequence,p.sequence)?"*":""}</td><td><span class="sequence-pill">${esc(seqText(c.sequence))}</span></td><td>${c.turns??"-"}</td><td>${c.lightScore??"-"}</td><td>${c.remainingTurnsAfter??"-"}</td><td>${c.remainingUnknownBossCount??"-"}</td><td>${c.cooldownCostAfter??"-"}</td><td>${c.readyDamageAfter??"-"}</td></tr>`).join("");return `<section class="boss-section"><h3>Boss ${p.bossIndex??"-"} HP ${p.revealedHp??"-"}</h3><div class="diagnosis">选中序列：<span class="sequence-pill">${esc(seqText(p.sequence))}</span>，回合 ${p.turns??"-"}，lightScore ${p.lightScore??"-"}，原因：${esc(p.selectionReason||"-")}</div><table class="debug-table"><thead><tr><th></th><th>候选技能序列</th><th>turns</th><th>lightScore</th><th>剩余回合</th><th>未知Boss</th><th>冷却成本</th><th>可用伤害</th></tr></thead><tbody>${rows||'<tr><td colspan="8">无候选记录</td></tr>'}</tbody></table></section>`}).join("");
$("bossEventTable").innerHTML=blocks||'<div class="diagnosis">没有 Boss phase 记录。</div>'
}
function setView(mode){viewMode=mode;$("boardView").classList.toggle("hidden",mode!="board");$("monitorView").classList.toggle("hidden",mode!="score");$("bossEventView").classList.toggle("hidden",mode!="boss");$("viewBoard").classList.toggle("active",mode=="board");$("viewScore").classList.toggle("active",mode=="score");$("viewBoss").classList.toggle("active",mode=="boss");draw()}
)HTML" + LR"HTML(
function draw(){
if(!maze)return;
ensureBoard();
const f=result?.frames?.[idx];
const preview=!result?.frames?.length||result?.mode=="resource-pickup-3x3";
ensureFrameCaches(preview,idx);
const lit=renderState.lit;
const visible=visibleCellsAt(f);
const vis=new Set(visible.map(c=>`${c.row},${c.col}`));
const path=renderState.path;
let cellIndex=0;
maze.forEach((r,i)=>r.forEach((t,j)=>{
const d=renderState.cells[cellIndex++];
const k=`${i},${j}`;
const isLit=lit.has(k);
let cls=`cell ${preview||isLit?tileClass(t):"unknown"}`;
if((preview||isLit)&&vis.has(k))cls+=" visible";
if((preview||isLit)&&path.has(k))cls+=" path";
if(f&&f.row==i&&f.col==j)cls+=" player";
if(d.className!==cls)d.className=cls;
const text=f&&f.row==i&&f.col==j?"P":((preview||isLit)&&t!="#"&&t!=" "?t:"");
if(d.textContent!==text)d.textContent=text
}));
$("resource").textContent=f?.resource??result?.resource??0;
$("steps").textContent=f?.step??result?.steps??0;
$("ratio").textContent=Number(result?.score_ratio??0).toFixed(2);
if(viewMode=="score")drawDebug();
if(viewMode=="boss")drawBossEvents()
}
)HTML" + LR"HTML(
function analyzeDebug(d,f,visible){
if(!d)return "当前帧没有贪心评分数据。请确认算法选择的是 3x3 实时贪心。";
if(d.pocket)return d.pocket.reason||"Pocket-aware first target greedy 已启用。";
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
$("debugSummary").innerHTML=d?`<div class="debug-grid"><div><span>决策</span><strong>${d.decision}</strong></div><div><span>${d.resourcePickup?"当前R/L":"alpha"}</span><strong>${fmt(d.resourcePickup?d.currentRatio:d.alpha)}</strong></div><div><span>${d.resourcePickup?"资源":"qEff"}</span><strong>${fmt(d.resourcePickup?d.resource:d.qEff)}</strong></div><div><span>${d.resourcePickup?"步数":"观察率"}</span><strong>${d.resourcePickup?d.step:fmt((d.observedRatio||0)*100,1)+"%"}</strong></div><div><span>当前位置</span><strong>${posText(d.resourcePickup?d.current:d.realCurrent)}</strong></div><div><span>选中目标</span><strong>${posText((d.candidates||[]).find(c=>c.selected)?.target||d.selectedReal)}</strong></div></div>`:"";
$("debugDiagnosis").textContent=analyzeDebug(d,f,visibleCellsAt(f));
if(!d){$("debugTable").innerHTML="";return}
if(d.resourcePickup){const rows=[...(d.candidates||[])].sort((a,b)=>b.score-a.score).map(c=>`<tr class="${c.selected?"selected ":""}${c.bundleValue>0?"gold-row":""}"><td>${c.selected?"* ":""}${c.cell||" "}</td><td>${posText(c.target)}</td><td>${c.cellValue}</td><td>${c.adjGoldCount}</td><td>${c.bundleValue}</td><td>${c.bundleLen}</td><td>${fmt(c.bundleScore)}</td><td>${fmt(c.projectedRatio)}</td><td>${c.actionable?"是":"否"}</td></tr>`).join("");$("debugDiagnosis").textContent=d.decision=="move"?"3x3 局部束贪心：只比较当前位置四邻域，且通过累计 R/L 保护。":"已停止：没有正收益局部束，或下一段会降低累计 R/L。";$("debugTable").innerHTML=`<table class="debug-table"><thead><tr><th>目标</th><th>坐标</th><th>cellValue</th><th>邻接G</th><th>BundleValue</th><th>BundleLen</th><th>BundleScore</th><th>projRatio</th><th>正收益</th></tr></thead><tbody>${rows}</tbody></table>`;return}
if(d.pocket){const p=d.pocket,rs=(p.pocketResources||[]).map(x=>posText(x.real)).join(" ");const rows=[...(p.candidates||[])].sort((a,b)=>b.scoreFirst-a.scoreFirst).map(c=>`<tr class="${c.selected?"selected ":""}gold-row"><td>${c.selected?"* ":""}G</td><td>${posText(c.realTarget)}</td><td>${c.pathLen}</td><td>${c.deltaR}</td><td>${fmt(c.baseScore)}</td><td>${fmt(c.ownIproxy)}</td><td>${posText(c.realBestRemainingTarget)}</td><td>${fmt(c.bestRemainingIproxy)}</td><td>${fmt(c.remainI)}</td><td>${fmt(c.scoreFirst)}</td></tr>`).join("");$("debugTable").innerHTML=`<div class="diagnosis">pocketHub=${posText(p.realPocketHub)} pocketResources=${rs} chosen=${posText(p.realChosenPocketTarget)}</div><table class="debug-table"><thead><tr><th>目标</th><th>坐标</th><th>len</th><th>deltaR</th><th>Base</th><th>ownIproxy</th><th>bestRemain</th><th>bestRemainI</th><th>remainI</th><th>Score_first</th></tr></thead><tbody>${rows}</tbody></table>`;return}
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
$("validate").onclick=async()=>{try{const parsed=parseInputMaze();const out=Array.isArray(parsed.data.grid)?await call("RunResourcePickup",parsed.input):await call("ValidateMaze",parsed.input);setView("boss");$("bossEventSummary").innerHTML="";$("bossEventTimeline").innerHTML=`<div class="diagnosis"><pre>${esc(JSON.stringify(out,null,2))}</pre></div>`;$("bossEventTable").innerHTML="";setState(out.ok?"校验通过":"校验失败")}catch(e){setState(e.message)}};
$("viewBoard").onclick=()=>setView("board");
$("viewScore").onclick=()=>setView("score");
$("viewBoss").onclick=()=>setView("boss");
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

/**
 * 功能：启用 Win32 桌面程序的高 DPI 感知。
 * 输入：
 *   - 无。
 * 输出：
 *   - 无返回值；系统不支持 Per-Monitor V2 时回退到系统 DPI 感知。
 * 关键逻辑：
 *   - WebView2 本身能清晰渲染文字，但如果宿主进程不是 DPI aware，Windows 会把整个窗口当作位图缩放，导致文字发虚。
 */
void enableDpiAwareness()
{
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }
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
        case WM_DPICHANGED: {
            const auto *suggested = reinterpret_cast<const RECT *>(lparam);
            if (suggested) {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            resizeWebView();
            return 0;
        }
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
    enableDpiAwareness();
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
