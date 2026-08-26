/**
 * @file WebConsole.cpp
 * @brief Async web console: telemetry streaming + full calibration GUI
 *
 * GUI capabilities:
 *  - Live telemetry @ 20 Hz (state, distance, gates, DSP, vactrols, relays)
 *  - Sensor calibration sliders (D_min/D_max/hysteresis/gamma/M/slew/clamps)
 *  - 6x vactrol sliders with AUTO/MANUAL toggle per channel
 *  - 8x relay cards: fire button + trigger dropdown + press length/count/gap
 */

#include "WebConsole.h"
#include "RelayManager.h"
#include "VactrolManager.h"
#include <Arduino.h>

/* ============================================================================
 * INLINE SPA
 * ============================================================================ */

static const char* INDEX_HTML = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>The Apparatus — WJ-AVE5 Console</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Cormorant Garamond',Georgia,serif;background:#F9F7F2;color:#113329;line-height:1.5}
.container{max-width:1400px;margin:0 auto;padding:16px}
header{text-align:center;padding:18px 0;border-bottom:2px solid #D4AF37;margin-bottom:16px}
h1{font-size:2rem;letter-spacing:2px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(340px,1fr));gap:14px}
.panel{background:#fff;border:1px solid #e0e0e0;border-radius:8px;padding:14px;box-shadow:0 2px 4px rgba(0,0,0,.05)}
.panel h2{font-size:1.1rem;border-bottom:1px solid #D4AF37;padding-bottom:8px;margin-bottom:10px}
.conn{display:flex;align-items:center;justify-content:center;gap:8px;padding:8px;margin-bottom:12px;border-radius:4px;font-size:.9rem}
.conn.ok{background:#e8f5e9;color:#2e7d32}.conn.bad{background:#fce4ec;color:#c62828}
.dot{width:10px;height:10px;border-radius:50%}.dot.ok{background:#4caf50}.dot.bad{background:#f44336}
.state-badge{display:inline-block;padding:10px 30px;border-radius:50px;font-size:1.3rem;font-weight:600;letter-spacing:2px}
.s0{background:#e8f5e9;color:#2e7d32}.s1{background:#e3f2fd;color:#1565c0}
.s2{background:#fff3e0;color:#e65100}.s3{background:#fce4ec;color:#c62828}
.metrics{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:12px}
.metric{background:#fafafa;padding:8px;border-radius:6px;border-left:3px solid #D4AF37}
.metric-label{font-size:.7rem;color:#666;text-transform:uppercase;letter-spacing:1px}
.metric-value{font-size:1.15rem;font-weight:600;font-family:monospace}
canvas{width:100%;height:160px;background:#fafafa;border:1px solid #e0e0e0;border-radius:4px}
.ebar-row{display:flex;align-items:center;gap:8px;margin:3px 0}
.ebar-label{width:86px;font-size:.72rem;color:#666;font-family:monospace}
.ebar-c{flex:1;height:18px;background:#f0f0f0;border-radius:9px;overflow:hidden}
.ebar{height:100%;background:linear-gradient(90deg,#D4AF37,#e8c56d)}
.ebar.peak{background:linear-gradient(90deg,#113329,#2e5c4d)}
.ebar-v{width:34px;text-align:right;font-size:.72rem;font-family:monospace;color:#666}
.slider-row{display:flex;align-items:center;gap:8px;margin:6px 0}
.slider-row label{flex:0 0 130px;font-size:.78rem;color:#555}
.slider-row input[type=range]{flex:1;accent-color:#113329}
.slider-row .val{flex:0 0 64px;text-align:right;font-family:monospace;font-size:.85rem}
.vac-card{border:1px solid #eee;border-radius:6px;padding:8px;margin:6px 0;background:#fcfcfc}
.vac-head{display:flex;justify-content:space-between;align-items:center;margin-bottom:4px}
.vac-name{font-weight:600;font-size:.9rem}
.vac-auto{display:flex;align-items:center;gap:4px;font-size:.75rem}
.relay-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.relay-card{border:1px solid #e5e5e5;border-radius:6px;padding:8px;background:#fcfcfc}
.relay-card.active{background:#fff8e1;border-color:#D4AF37}
.relay-name{font-weight:700;font-size:.85rem;display:flex;justify-content:space-between}
.relay-dot{width:10px;height:10px;border-radius:50%;background:#ccc}
.relay-dot.on{background:#e65100;box-shadow:0 0 6px #ff9800}
.relay-card select,.relay-card input[type=number]{width:100%;padding:4px;margin:3px 0;border:1px solid #ddd;border-radius:4px;font-size:.8rem}
.mini-btn{width:100%;padding:7px;margin-top:4px;background:#113329;color:#F9F7F2;border:none;border-radius:4px;font-family:inherit;font-size:.85rem;cursor:pointer}
.mini-btn:hover{background:#0d261e}
.mini-btn.stop{background:#8b0000}
.btn{width:100%;padding:11px;background:#113329;color:#F9F7F2;border:none;border-radius:4px;font-family:inherit;font-size:1rem;cursor:pointer;margin-top:8px}
.note{font-size:.72rem;color:#888;margin-top:4px}
</style></head><body><div class="container">
<header><h1>THE APPARATUS</h1><p style="color:#666;font-style:italic">Panasonic WJ-AVE5 — Vactrol &amp; Relay Console</p></header>
<div class="conn bad" id="conn"><span class="dot bad"></span><span id="connTxt">Connecting...</span></div>
<div class="grid">

<div class="panel"><h2>System State</h2>
<div style="text-align:center"><span class="state-badge s0" id="badge">IDLE</span></div>
<div class="metrics">
<div class="metric"><div class="metric-label">Mix PWM</div><div class="metric-value" id="mMix">0</div></div>
<div class="metric"><div class="metric-label">Pi Trigger</div><div class="metric-value" id="mPi">LOW</div></div>
<div class="metric"><div class="metric-label">Dist Raw</div><div class="metric-value" id="mDr">—</div></div>
<div class="metric"><div class="metric-label">Dist Filt</div><div class="metric-value" id="mDf">—</div></div>
<div class="metric"><div class="metric-label">Base PWM</div><div class="metric-value" id="mBp">0</div></div>
<div class="metric"><div class="metric-label">Gamma</div><div class="metric-value" id="mGs">0</div></div>
</div></div>

<div class="panel"><h2>Radar Gates (stationary)</h2><div id="gates"></div>
<div class="note">◄ = peak gate used by DSP centroid interpolation</div></div>

<div class="panel" style="grid-column:span 2"><h2>Radar Preview — Top-Down View</h2>
<canvas id="radar" width="800" height="300"></canvas>
<div style="display:flex;justify-content:space-around;font-size:.72rem;color:#666;margin-top:4px">
<span>radar at bottom-center</span><span><span style="color:#2e7d32">━</span> target</span><span><span style="color:#1565c0">┅</span> Pi zones (near/far)</span></div></div>

<div class="panel" style="grid-column:span 2"><h2>Breathing Oscilloscope — Biquad / AGC</h2>
<canvas id="scope" width="800" height="160"></canvas>
<div style="display:flex;justify-content:space-around;font-size:.72rem;color:#666;margin-top:4px">
<span><span style="color:#1565c0">━</span> Biquad raw</span><span><span style="color:#e65100">━</span> AGC normalized ±1.0</span></div></div>

<div class="panel" style="grid-column:span 2"><h2>Vactrol Channels — WJ-AVE5 Sliders</h2>
<div id="vactrols"></div><div class="note">AUTO channels are driven by the radar state machine (Mix) or reserved logic. MANUAL gives you the slider.</div></div>

<div class="panel"><h2>Sensor Calibration</h2><div id="calib"></div>
<button class="btn" id="saveBtn">Save to NVS</button>
<button class="btn" id="resetBtn" style="background:#666">Factory Reset</button></div>

<div class="panel" style="grid-column:span 2"><h2>Relay Bank — WJ-AVE5 Buttons</h2>
<div class="relay-grid" id="relays"></div></div>

<div class="panel" style="grid-column:span 2"><h2>Boot Sequence — WJ-AVE5 Power-On Ritual</h2>
<div class="note" style="margin-bottom:8px">Runs automatically at power-up after the start delay (let the mixer PSU stabilize). Each step presses a relay N times with configurable length/gap, then dwells. Persisted in NVS.</div>
<div id="bootpanel"></div></div>

</div></div>
<script>
let ws=null,retries=0;
const SCOPE_N=200;let bq=new Array(SCOPE_N).fill(0),agc=new Array(SCOPE_N).fill(0);
const cv=document.getElementById('scope'),cx=cv.getContext('2d');
const CW=cv.width,CH=cv.height,CY=CH/2,SC=CH*.42;
const rv=document.getElementById('radar'),rx=rv.getContext('2d');
const RW=rv.width,RH=rv.height,RBASE_Y=RH-20;RMAX_CM=675;RSCALE=(RH-40)/RMAX_CM;
const TRIGGERS=["Manual","Layer 1 Return (Idle)","Layer 2 Entry (Macro)","Breath Lock (Micro)","Layer 3 Cut (Contact)","Inhale Peak","Exhale Peak"];
let AVE5_BUTTONS=[],AVE5_POTS=[];   // filled from config payload
const VAC_NAMES=["Mix/T-Bar","Color X","Color Y","Wipe Speed","Effect Level","Aux Mod"];
const PIN_OPTIONS=[4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33];

function connect(){
 ws=new WebSocket((location.protocol==='https:'?'wss:':'ws:')+'//'+location.host+'/ws');
 ws.onopen=()=>{setConn(true);retries=0;ws.send(JSON.stringify({type:'get_config'}))};
 ws.onclose=()=>{setConn(false);if(retries++<10)setTimeout(connect,2000)};
 ws.onmessage=e=>{try{handle(JSON.parse(e.data))}catch(err){}};
}
function setConn(ok){document.getElementById('conn').className='conn '+(ok?'ok':'bad');
 document.querySelector('#conn .dot').className='dot '+(ok?'ok':'bad');
 document.getElementById('connTxt').textContent=ok?'Connected — live @ 20 Hz':'Reconnecting...'}

function handle(m){
 if(m.type==='telemetry')drawTele(m.payload);
 else if(m.type==='config'){buildUI(m.payload);}
 else if(m.type==='saved'){flash('Saved to NVS');}
}

function drawTele(p){
 const b=document.getElementById('badge');b.textContent=p.state_name;b.className='state-badge s'+p.state;
 document.getElementById('mMix').textContent=p.mix_pwm;
 document.getElementById('mPi').textContent=p.pi_trigger?'HIGH':'LOW';
 document.getElementById('mDr').textContent=p.distance_raw+' cm';
 document.getElementById('mDf').textContent=p.distance_filtered.toFixed(1)+' cm';
 document.getElementById('mBp').textContent=p.base_pwm_f.toFixed(0);
 document.getElementById('mGs').textContent=p.gamma_shaped.toFixed(3);
 // gates
 const mx=Math.max(...p.stationary_energy,1);
 document.getElementById('gates').innerHTML=p.stationary_energy.map((v,i)=>{
  const pk=i===p.peak_gate,st=i*75,en=st+75;
  return `<div class="ebar-row"><div class="ebar-label">G${i} ${st}-${en}</div><div class="ebar-c"><div class="ebar ${pk?'peak':''}" style="width:${(v/mx)*100}%"></div></div><div class="ebar-v">${v}${pk?'◄':''}</div></div>`}).join('');
 // scope
 bq.push(p.biquad_raw);bq.shift();agc.push(p.agc_normalized);agc.shift();
 cx.clearRect(0,0,CW,CH);cx.strokeStyle='#eee';
 for(let y=0;y<=CH;y+=20){cx.beginPath();cx.moveTo(0,y);cx.lineTo(CW,y);cx.stroke()}
 cx.strokeStyle='#ddd';cx.beginPath();cx.moveTo(0,CY);cx.lineTo(CW,CY);cx.stroke();
 const trace=(buf,col,w,scale)=>{cx.strokeStyle=col;cx.lineWidth=w;cx.beginPath();
  buf.forEach((v,i)=>{const x=i/SCOPE_N*CW,y=CY-v*SC;i?cx.lineTo(x,y):cx.moveTo(x,y)});cx.stroke()};
 trace(bq,'#1565c0',1.2,.5);trace(agc,'#e65100',2,1);
 // radar top-down preview
 drawRadar(p);
 // relays live dots
 p.relay_pressed.forEach((on,i)=>{const d=document.getElementById('rdot'+i);
   const c=document.getElementById('rcard'+i);
   if(d)d.className='relay-dot'+(on?' on':'');if(c)c.classList.toggle('active',!!p.relay_seq[i])});
 // vactrol values
 p.vactrol_val.forEach((v,i)=>{const el=document.getElementById('vv'+i);if(el&&!dragging[i])el.textContent=v});
}

function drawRadar(p){
 const cm2y=cm=>RBASE_Y-cm*RSCALE;
 rx.clearRect(0,0,RW,RH);
 // gate arcs every 75cm + labels
 rx.strokeStyle='#e8e8e8';rx.fillStyle='#999';rx.font='11px sans-serif';
 for(let g=1;g<=9;g++){const y=cm2y(g*75);
  rx.beginPath();rx.moveTo(RW/2-40,y);rx.lineTo(RW/2+40,y);rx.stroke();
  if(g%2===0)rx.fillText(g*75+'cm',RW/2+46,y+4)}
 // D_max / D_min bands
 const dmn=CFG?CFG.D_min:60,dmx=CFG?CFG.D_max:450;
 rx.fillStyle='rgba(211,47,47,0.05)';
 rx.fillRect(RW/2-260,cm2y(dmx),520,cm2y(dmn)-cm2y(dmx));
 rx.strokeStyle='#c62828';rx.setLineDash([6,5]);
 [dmn,dmx].forEach(d=>{const y=cm2y(d);rx.beginPath();rx.moveTo(RW/2-260,y);rx.lineTo(RW/2+260,y);rx.stroke()});
 rx.setLineDash([]);rx.fillStyle='#c62828';
 rx.fillText('D_min '+dmn+'cm',RW/2-330,cm2y(dmn)+4);
 rx.fillText('D_max '+dmx+'cm',RW/2-330,cm2y(dmx)+4);
 // pi zones
 if(CFG){[['pi_zone_near_cm','#1565c0'],['pi_zone_far_cm','#1565c0']].forEach(([k])=>{
  const y=cm2y(CFG[k]);rx.strokeStyle=k==='pi_zone_near_cm'?'#64b5f6':'#1565c0';
  rx.setLineDash([3,4]);rx.beginPath();rx.moveTo(RW/2-180,y);rx.lineTo(RW/2+180,y);rx.stroke();rx.setLineDash([]);
  rx.fillStyle=k==='pi_zone_near_cm'?'#64b5f6':'#1565c0';rx.fillText(k.replace('pi_zone_','').replace('_cm','')+' zone',RW/2+190,y+4)})}
 // radar module
 rx.fillStyle='#333';rx.fillRect(RW/2-30,RBASE_Y-8,60,16);
 rx.fillStyle='#fff';rx.font='10px sans-serif';rx.fillText('LD2410',RW/2-20,RBASE_Y+3);rx.font='11px sans-serif';
 // target
 const st=p.state,dc=p.distance_filtered||p.distance_raw||0;
 if(st>0&&st<3&&dc>0){const ty=cm2y(Math.min(dc,RMAX_CM));
  const col=st===1?'#f9a825':st===2?'#2e7d32':'#d32f2f';
  rx.beginPath();rx.arc(RW/2,ty,14,0,Math.PI*2);rx.fillStyle=col;rx.fill();
  rx.fillStyle='#fff';rx.textAlign='center';rx.fillText((st===2?'♥ ':'')+Math.round(dc),RW/2,ty+4);
  rx.textAlign='left';
  // state ring
  rx.beginPath();rx.arc(RW/2,ty,20,0,Math.PI*2);rx.strokeStyle=col;rx.lineWidth=2;rx.stroke()}
 else{rx.fillStyle='#bbb';if(p.state===3){rx.fillText('CONTACT - live camera',RW/2-70,RH/2)}
  else if(p.state===0){const ty=cm2y(Math.min(dc||600,RMAX_CM));rx.beginPath();
   rx.arc(RW/2,ty,8,0,Math.PI*2);rx.fillStyle='#ddd';rx.fill()}}
}
let CFG=null;const dragging={};
function buildUI(c){CFG=c;
 AVE5_BUTTONS=c.ave5_buttons||AVE5_BUTTONS;
 AVE5_POTS=c.ave5_pots||AVE5_POTS;
 // calibration sliders
 const cal=[['D_min','D_min (cm)',10,500,1,'cm'],['D_max','D_max (cm)',50,675,1,'cm'],
 ['hysteresis','Hysteresis',0,100,1,'cm'],['gamma_exponent','Gamma γ',0.1,4,0.01,''],
 ['breathing_depth_M','Breath depth M',0,1,0.01,''],['slew_rate_limit','Slew rate',0.1,20,0.1,'/ms×.01'],
 ['variance_threshold_cm','Variance thr',1,30,0.5,'cm'],['breath_threshold','Breath trig thr',0.1,1,0.05,'σ'],
 ['pi_zone_near_cm','Pi zone near',40,400,5,'cm'],['pi_zone_far_cm','Pi zone far',100,650,5,'cm']];
 document.getElementById('calib').innerHTML=cal.map(([k,l,mn,mx,st,u])=>
  `<div class="slider-row"><label>${l}</label><input type="range" id="c_${k}" min="${mn}" max="${mx}" step="${st}" value="${c[k]}" oninput="upd('${k}',this.value)"><span class="val" id="cv_${k}">${c[k]}${u}</span></div>`).join('')
 +`<div class="slider-row"><label>PWM min/max clamp</label>
 <input type="number" id="c_pwm_min_clamp" min="0" max="254" value="${c.pwm_min_clamp}" style="width:70px">
 <input type="number" id="c_pwm_max_clamp" min="1" max="255" value="${c.pwm_max_clamp}" style="width:70px"></div>`;
 // vactrols
 document.getElementById('vactrols').innerHTML=VAC_NAMES.map((n,i)=>{
  const v=c.vactrol[i];
  return `<div class="vac-card"><div class="vac-head"><span class="vac-name">${i+1}. ${n}</span>
  <label class="vac-auto"><input type="checkbox" id="va_${i}" ${v.auto_mode?'checked':''} onchange="setVacAuto(${i},this.checked)"> AUTO</label></div>
  <div class="slider-row"><label>Drives</label><select id="vpot_${i}" style="flex:1" onchange="setVacPot(${i})">${AVE5_POTS.map((p,pi)=>`<option value="${pi}" ${v.ave5_pot===pi?'selected':''}>${p}</option>`).join('')}</select></div>
  <div class="slider-row"><label>Manual level</label><input type="range" id="vs_${i}" min="0" max="1023" value="${v.manual_value}" oninput="setVac(${i},this.value)" ${v.auto_mode?'disabled':''}><span class="val" id="vv${i}">${v.manual_value}</span></div>
  <div class="slider-row"><label>Clamp min / max</label>
  <input type="number" id="vcmin_${i}" min="0" max="1022" value="${v.min_clamp}" style="width:80px">
  <input type="number" id="vcmax_${i}" min="1" max="1023" value="${v.max_clamp}" style="width:80px">
  <input type="number" id="vslew_${i}" step="0.1" min="0.1" value="${v.slew_per_ms}" title="slew/ms" style="width:80px"></div></div>`}).join('');
 // relays
 document.getElementById('relays').innerHTML=c.fx.map((r,i)=>
  `<div class="relay-card" id="rcard${i}"><div class="relay-name">${r.name}<span class="relay-dot" id="rdot${i}"></span></div>
  <select id="rb_${i}" title="WJ-AVE5 button this relay presses" onchange="setRelayBtn(${i})">${AVE5_BUTTONS.map((b,bi)=>`<option value="${bi}" ${r.ave5_button===bi?'selected':''}>${b}</option>`).join('')}</select>
  <select id="rt_${i}" onchange="setRelayCfg(${i})">${TRIGGERS.map((t,ti)=>`<option value="${ti}" ${r.trigger===ti?'selected':''}>${t}</option>`).join('')}</select>
  <div style="display:flex;gap:4px">
  <input type="number" id="rl_${i}" min="30" max="3000" step="10" value="${r.press_length_ms}" title="press ms" onchange="setRelayCfg(${i})">
  <input type="number" id="rn_${i}" min="1" max="5" value="${r.press_count}" title="presses (2=double click)" onchange="setRelayCfg(${i})">
  <input type="number" id="rg_${i}" min="20" max="2000" step="10" value="${r.press_gap_ms}" title="gap ms" onchange="setRelayCfg(${i})"></div>
  <div style="display:flex;gap:4px;align-items:center;margin:3px 0">
  <label style="font-size:.72rem;flex:1"><input type="checkbox" id="rclk_${i}" ${r.clock_enable?'checked':''} onchange="setRelayCfg(${i},true)"> clock</label>
  <input type="number" id="rci_${i}" min="500" max="600000" step="500" value="${r.clock_interval_ms}" title="clock interval ms" onchange="setRelayCfg(${i},true)" ${r.clock_enable?'':'disabled'} style="width:90px"></div>
  <div style="display:flex;gap:4px;align-items:center;margin:3px 0">
  <label style="font-size:.72rem">GPIO</label>
  <select id="rpin_${i}" onchange="remapPin(${i})" style="flex:1">${PIN_OPTIONS.map(p=>`<option ${r.pin===p?'selected':''}>${p}</option>`).join('')}</select></div>
  <button class="mini-btn" onclick="fireRelay(${i})">⚡ FIRE</button>
  <button class="mini-btn stop" onclick="stopRelay(${i})">■ STOP</button></div>`).join('');

 // boot sequence editor
 const bt=c.boot;
 let bootHtml=`<div class="slider-row"><label>Enabled</label><input type="checkbox" id="bt_en" ${bt.enabled?'checked':''}></div>
 <div class="slider-row"><label>Start delay</label><input type="range" id="bt_delay" min="500" max="30000" step="250" value="${bt.start_delay_ms}" oninput="document.getElementById('bt_delay_v').textContent=this.value+'ms'"><span class="val" id="bt_delay_v">${bt.start_delay_ms}ms</span></div>`;
 for(let i=0;i<BOOT_STEPS_MAX;i++){
  const st=bt.steps[i],active=i<bt.step_count;
  bootHtml+=`<div class="vac-card" style="${active?'':'opacity:.45'}">
  <div class="vac-head"><b>Step ${i+1}</b>
  <label style="font-size:.72rem"><input type="checkbox" id="bs_on_${i}" ${active?'checked':''}> use</label></div>
  <div style="display:flex;gap:4px">
  <select id="bs_r_${i}" title="relay" style="width:32%">${c.fx.map((r,ri)=>`<option value="${ri}" ${st.relay===ri?'selected':''}>${r.name}</option>`).join('')}</select>
  <input type="number" id="bs_p_${i}" min="1" max="5" value="${st.presses}" title="presses (2=double)" style="width:22%">
  <input type="number" id="bs_l_${i}" min="30" max="5000" step="10" value="${st.length_ms}" title="length ms" style="width:22%">
  <input type="number" id="bs_g_${i}" min="20" max="5000" step="10" value="${st.gap_ms}" title="gap ms" style="width:22%"></div>
  <div class="slider-row"><label>wait after</label><input type="range" id="bs_w_${i}" min="50" max="20000" step="50" value="${st.wait_after_ms}"><span class="val">${st.wait_after_ms}ms</span></div></div>`;
 }
 bootHtml+=`<button class="btn" onclick="replayBoot()">▶ REPLAY BOOT SEQUENCE NOW</button>`;
 document.getElementById('bootpanel').innerHTML=bootHtml;
 window.BOOT_STEPS_MAX=BOOT_STEPS_MAX;
}
const BOOT_STEPS_MAX=12;
function upd(k,v){CFG[k]=parseFloat(v);const u={D_min:'cm',D_max:'cm',hysteresis:'cm'}[k]||'';
 document.getElementById('cv_'+k).textContent=v+u;}
function setVac(i,v){ws.send(JSON.stringify({type:'vactrol_manual',ch:i,value:parseInt(v)}));document.getElementById('vv'+i).textContent=v}
function setVacAuto(i,on){document.getElementById('vs_'+i).disabled=on;
 ws.send(JSON.stringify({type:'vactrol_auto',ch:i,auto:on}))}
function setRelayCfg(i,clockOnly){
 if(clockOnly){const en=document.getElementById('rclk_'+i).checked;
  document.getElementById('rci_'+i).disabled=!en;
  ws.send(JSON.stringify({type:'relay_cfg',index:i,clock_enable:en,
   clock_interval_ms:parseInt(document.getElementById('rci_'+i).value)}));return}
 ws.send(JSON.stringify({type:'relay_cfg',index:i,
  trigger:parseInt(document.getElementById('rt_'+i).value),
  press_length_ms:parseInt(document.getElementById('rl_'+i).value),
  press_count:parseInt(document.getElementById('rn_'+i).value),
  press_gap_ms:parseInt(document.getElementById('rg_'+i).value)}))}
function remapPin(i){ws.send(JSON.stringify({type:'relay_pin',index:i,
 pin:parseInt(document.getElementById('rpin_'+i).value)}))}
function setRelayBtn(i){ws.send(JSON.stringify({type:'relay_ave5_button',index:i,
 button:parseInt(document.getElementById('rb_'+i).value)}))}
function setVacPot(i){ws.send(JSON.stringify({type:'vactrol_pot',ch:i,
 pot:parseInt(document.getElementById('vpot_'+i).value)}))}
function replayBoot(){ws.send(JSON.stringify({type:'boot_replay'}))}
function fireRelay(i){ws.send(JSON.stringify({type:'relay_fire',index:i}))}
function stopRelay(i){ws.send(JSON.stringify({type:'relay_stop',index:i}))}
function flash(t){const e=document.createElement('div');e.textContent=t;e.style.cssText='position:fixed;top:10px;right:10px;background:#113329;color:#fff;padding:8px 16px;border-radius:4px';document.body.appendChild(e);setTimeout(()=>e.remove(),2500)}
document.getElementById('saveBtn').onclick=()=>{
 const p={};['D_min','D_max','hysteresis','gamma_exponent','breathing_depth_M','slew_rate_limit',
 'variance_threshold_cm','breath_threshold','pi_zone_near_cm','pi_zone_far_cm'].forEach(k=>p[k]=CFG[k]);
 p.pwm_min_clamp=parseInt(document.getElementById('c_pwm_min_clamp').value);
 p.pwm_max_clamp=parseInt(document.getElementById('c_pwm_max_clamp').value);
 p.vactrol=[];for(let i=0;i<6;i++)p.vactrol.push({auto_mode:document.getElementById('va_'+i).checked,
  min_clamp:parseInt(document.getElementById('vcmin_'+i).value),
  max_clamp:parseInt(document.getElementById('vcmax_'+i).value),
  slew_per_ms:parseFloat(document.getElementById('vslew_'+i).value),
  manual_value:parseInt(document.getElementById('vs_'+i).value),
  ave5_pot:parseInt(document.getElementById('vpot_'+i).value)});
 p.fx=CFG.fx.map((r,i)=>({trigger:parseInt(document.getElementById('rt_'+i).value),
  press_length_ms:parseInt(document.getElementById('rl_'+i).value),
  press_count:parseInt(document.getElementById('rn_'+i).value),
  press_gap_ms:parseInt(document.getElementById('rg_'+i).value),
  clock_enable:document.getElementById('rclk_'+i).checked,
  clock_interval_ms:parseInt(document.getElementById('rci_'+i).value),
  ave5_button:parseInt(document.getElementById('rb_'+i).value)}));
 // boot sequence
 const stepsOn=[];for(let i=0;i<BOOT_STEPS_MAX;i++){
  if(document.getElementById('bs_on_'+i).checked)stepsOn.push(i)}
 const allSteps=[];
 for(let i=0;i<BOOT_STEPS_MAX;i++){allSteps.push({relay:parseInt(document.getElementById('bs_r_'+i).value),
  presses:parseInt(document.getElementById('bs_p_'+i).value),
  length_ms:parseInt(document.getElementById('bs_l_'+i).value),
  gap_ms:parseInt(document.getElementById('bs_g_'+i).value),
  wait_after_ms:parseInt(document.getElementById('bs_w_'+i).value)})}
 const ordered=stepsOn.map(i=>allSteps[i]);
 p.boot={enabled:document.getElementById('bt_en').checked,
  start_delay_ms:parseInt(document.getElementById('bt_delay').value),
  step_count:ordered.length,steps:ordered};
 ws.send(JSON.stringify({type:'save_config',payload:p}))};
document.getElementById('resetBtn').onclick=()=>confirm('Factory reset?')&&ws.send(JSON.stringify({type:'factory_reset'}));
connect();
for(let i=0;i<9;i++)document.getElementById('gates').innerHTML+=`<div class="ebar-row"><div class="ebar-label">G${i} ${i*75}-${i*75+75}</div><div class="ebar-c"><div class="ebar" style="width:0%"></div></div><div class="ebar-v">0</div></div>`;
</script></body></html>)HTML";

/* ============================================================================
 * LIFECYCLE
 * ============================================================================ */

WebConsole::WebConsole(uint16_t port) : _port(port) {}
WebConsole::~WebConsole() { end(); }

bool WebConsole::begin() {
    _server = new AsyncWebServer(_port);
    _ws = new AsyncWebSocket("/ws");
    if (!_server || !_ws) return false;

    _ws->onEvent([this](AsyncWebSocket* srv, AsyncWebSocketClient* cli,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
        _onWsEvent(srv, cli, type, arg, data, len);
    });
    _server->addHandler(_ws);

    _server->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", INDEX_HTML);
    });
    _server->onNotFound([](AsyncWebServerRequest* req) { req->send(404); });

    _server->begin();
    log_i("WebConsole on :%u", _port);
    return true;
}

void WebConsole::end() {
    if (_ws) { _ws->closeAll(); delete _ws; _ws = nullptr; }
    if (_server) { _server->end(); delete _server; _server = nullptr; }
}

void WebConsole::_setupRoutes() {}

/* ============================================================================
 * WEBSOCKET EVENTS
 * ============================================================================ */

void WebConsole::_onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
                            AwsEventType type, void*, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            log_i("WS client #%u connected", client->id());
            _sendFullConfig(client);
            break;
        case WS_EVT_DISCONNECT:
            log_i("WS client #%u gone", client->id());
            break;
        case WS_EVT_DATA:
            _handleWsMessage(client, data, len);
            break;
        default: break;
    }
}

extern RelayManager g_relays;      // main.cpp
extern VactrolManager g_vactrols;  // main.cpp

void WebConsole::_sendFullConfig(AsyncWebSocketClient* client) {
    JsonDocument doc;
    doc["type"] = "config";
    JsonObject c = doc["payload"].to<JsonObject>();

    c["D_min"] = g_config.D_min;
    c["D_max"] = g_config.D_max;
    c["hysteresis"] = g_config.hysteresis;
    c["gamma_exponent"] = g_config.gamma_exponent;
    c["breathing_depth_M"] = g_config.breathing_depth_M;
    c["slew_rate_limit"] = g_config.slew_rate_limit;
    c["variance_threshold_cm"] = g_config.variance_threshold_cm;
    c["breath_threshold"] = g_config.breath_threshold;
    c["pi_zone_near_cm"] = g_config.pi_zone_near_cm;
    c["pi_zone_far_cm"] = g_config.pi_zone_far_cm;
    c["pwm_min_clamp"] = g_config.pwm_min_clamp;
    c["pwm_max_clamp"] = g_config.pwm_max_clamp;

    // WJ-AVE5 control-surface catalogs for the GUI dropdowns
    JsonArray abtns = c["ave5_buttons"].to<JsonArray>();
    for (unsigned b = 0; b < AVE5_BUTTON_COUNT; b++) abtns.add(AVE5_BUTTONS[b]);
    JsonArray apots = c["ave5_pots"].to<JsonArray>();
    for (unsigned q = 0; q < AVE5_POT_COUNT; q++) apots.add(AVE5_POTS[q]);

    JsonArray vac = c["vactrol"].to<JsonArray>();
    for (int i = 0; i < VACTROL_COUNT; i++) {
        JsonObject v = vac.add<JsonObject>();
        v["auto_mode"] = g_config.vactrol[i].auto_mode;
        v["min_clamp"] = g_config.vactrol[i].min_clamp;
        v["max_clamp"] = g_config.vactrol[i].max_clamp;
        v["slew_per_ms"] = g_config.vactrol[i].slew_per_ms;
        v["manual_value"] = g_config.vactrol[i].manual_value;
        v["ave5_pot"] = g_config.vactrol[i].ave5_pot;
    }

    JsonArray fx = c["fx"].to<JsonArray>();
    for (int i = 0; i < RELAY_COUNT; i++) {
        JsonObject r = fx.add<JsonObject>();
        r["name"] = g_config.fx[i].name;
        r["trigger"] = g_config.fx[i].trigger;
        r["press_length_ms"] = g_config.fx[i].press_length_ms;
        r["press_count"] = g_config.fx[i].press_count;
        r["press_gap_ms"] = g_config.fx[i].press_gap_ms;
        r["pin"] = g_relays.getPin(i);
        r["clock_enable"] = g_config.fx[i].clock_enable;
        r["clock_interval_ms"] = g_config.fx[i].clock_interval_ms;
        r["ave5_button"] = g_config.fx[i].ave5_button;
    }

    // Boot sequence
    JsonObject boot = c["boot"].to<JsonObject>();
    boot["enabled"] = g_config.boot.enabled;
    boot["start_delay_ms"] = g_config.boot.start_delay_ms;
    boot["step_count"] = g_config.boot.step_count;
    JsonArray steps = boot["steps"].to<JsonArray>();
    for (int i = 0; i < BOOT_MAX_STEPS; i++) {
        JsonObject st = steps.add<JsonObject>();
        st["relay"] = g_config.boot.steps[i].relay;
        st["presses"] = g_config.boot.steps[i].presses;
        st["length_ms"] = g_config.boot.steps[i].length_ms;
        st["gap_ms"] = g_config.boot.steps[i].gap_ms;
        st["wait_after_ms"] = g_config.boot.steps[i].wait_after_ms;
    }

    String out; serializeJson(doc, out);
    client->text(out);
}

extern RelayManager g_relays;      // main.cpp
extern VactrolManager g_vactrols;  // main.cpp

void WebConsole::_handleWsMessage(AsyncWebSocketClient* client, uint8_t* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) return;

    const char* type = doc["type"] | "";
    String respType = "ok";

    if (!strcmp(type, "get_config")) {
        _sendFullConfig(client);
        return;
    }
    else if (!strcmp(type, "relay_fire")) {
        uint8_t idx = doc["index"] | 255;
        if (_relay_fire && idx < RELAY_COUNT) _relay_fire(idx);
    }
    else if (!strcmp(type, "relay_stop")) {
        uint8_t idx = doc["index"] | 255;
        if (_relay_stop && idx < RELAY_COUNT) _relay_stop(idx);
    }
    else if (!strcmp(type, "relay_cfg")) {
        uint8_t idx = doc["index"] | 255;
        if (idx < RELAY_COUNT) {
            FxRelaySettings& s = g_config.fx[idx];
            s.trigger           = constrain(doc["trigger"].as<int>(), 0, FX_TRIGGER_COUNT - 1);
            s.press_length_ms   = constrain(doc["press_length_ms"].as<int>(), 30, 3000);
            s.press_count       = constrain(doc["press_count"].as<int>(), 1, 5);
            s.press_gap_ms      = constrain(doc["press_gap_ms"].as<int>(), 20, 2000);
            if (!doc["ave5_button"].isNull())
                s.ave5_button   = constrain(doc["ave5_button"].as<int>(), 0, (int)AVE5_BUTTON_COUNT - 1);
            if (!doc["clock_enable"].isNull())
                s.clock_enable = doc["clock_enable"] | false;
            if (!doc["clock_interval_ms"].isNull())
                s.clock_interval_ms = constrain(doc["clock_interval_ms"].as<int>(), 500, 600000);
        }
    }
    else if (!strcmp(type, "relay_ave5_button")) {
        uint8_t idx = doc["index"] | 255;
        if (idx < RELAY_COUNT)
            g_config.fx[idx].ave5_button =
                constrain(doc["button"].as<int>(), 0, (int)AVE5_BUTTON_COUNT - 1);
    }
    else if (!strcmp(type, "relay_pin")) {
        uint8_t idx = doc["index"] | 255;
        uint8_t pin = doc["pin"] | 0;
        extern RelayManager g_relays;   // main.cpp global
        bool ok = (idx < RELAY_COUNT) && g_relays.remapPin(idx, pin);
        JsonDocument ack2; ack2["type"] = ok ? "pin_ok" : "pin_fail";
        String o2; serializeJson(ack2, o2); client->text(o2);
        return;
    }
    else if (!strcmp(type, "vactrol_auto")) {
        uint8_t ch = doc["ch"] | 255;
        if (ch < VACTROL_COUNT) g_config.vactrol[ch].auto_mode = doc["auto"] | false;
    }
    else if (!strcmp(type, "vactrol_manual")) {
        uint8_t ch = doc["ch"] | 255;
        if (ch < VACTROL_COUNT) {
            g_config.vactrol[ch].manual_value =
                constrain(doc["value"].as<int>(), 0, VACTROL_PWM_MAX);
        }
    }
    else if (!strcmp(type, "vactrol_pot")) {
        uint8_t ch = doc["ch"] | 255;
        if (ch < VACTROL_COUNT)
            g_config.vactrol[ch].ave5_pot =
                constrain(doc["pot"].as<int>(), 0, (int)AVE5_POT_COUNT - 1);
    }
    else if (!strcmp(type, "save_config")) {
        JsonObject p = doc["payload"];
        g_config.D_min              = p["D_min"] | g_config.D_min;
        g_config.D_max              = p["D_max"] | g_config.D_max;
        g_config.hysteresis         = p["hysteresis"] | g_config.hysteresis;
        g_config.gamma_exponent     = p["gamma_exponent"] | g_config.gamma_exponent;
        g_config.breathing_depth_M  = p["breathing_depth_M"] | g_config.breathing_depth_M;
        g_config.slew_rate_limit    = p["slew_rate_limit"] | g_config.slew_rate_limit;
        g_config.variance_threshold_cm = p["variance_threshold_cm"] | g_config.variance_threshold_cm;
        g_config.breath_threshold   = p["breath_threshold"] | g_config.breath_threshold;
        g_config.pi_zone_near_cm    = p["pi_zone_near_cm"] | g_config.pi_zone_near_cm;
        g_config.pi_zone_far_cm     = p["pi_zone_far_cm"] | g_config.pi_zone_far_cm;
        g_config.pwm_min_clamp      = p["pwm_min_clamp"] | g_config.pwm_min_clamp;
        g_config.pwm_max_clamp      = p["pwm_max_clamp"] | g_config.pwm_max_clamp;

        JsonArray vac = p["vactrol"].as<JsonArray>();
        int i = 0;
        for (JsonObject v : vac) {
            if (i >= VACTROL_COUNT) break;
            g_config.vactrol[i].auto_mode    = v["auto_mode"] | g_config.vactrol[i].auto_mode;
            g_config.vactrol[i].min_clamp    = v["min_clamp"] | g_config.vactrol[i].min_clamp;
            g_config.vactrol[i].max_clamp    = v["max_clamp"] | g_config.vactrol[i].max_clamp;
            g_config.vactrol[i].slew_per_ms  = v["slew_per_ms"] | g_config.vactrol[i].slew_per_ms;
            g_config.vactrol[i].manual_value = v["manual_value"] | g_config.vactrol[i].manual_value;
            g_config.vactrol[i].ave5_pot     = v["ave5_pot"] | g_config.vactrol[i].ave5_pot;
            i++;
        }

        // Relay bank: persisted here so GUI edits survive reboots
        JsonArray fxp = p["fx"].as<JsonArray>();
        i = 0;
        if (!fxp.isNull()) {
            for (JsonObject r : fxp) {
                if (i >= RELAY_COUNT) break;
                g_config.fx[i].trigger         = constrain(r["trigger"].as<int>(), 0, FX_TRIGGER_COUNT - 1);
                g_config.fx[i].press_length_ms = constrain(r["press_length_ms"].as<int>(), 30, 3000);
                g_config.fx[i].press_count     = constrain(r["press_count"].as<int>(), 1, 5);
                g_config.fx[i].press_gap_ms    = constrain(r["press_gap_ms"].as<int>(), 20, 2000);
                g_config.fx[i].clock_enable    = r["clock_enable"] | g_config.fx[i].clock_enable;
                g_config.fx[i].clock_interval_ms = constrain(r["clock_interval_ms"].as<int>() | (int)g_config.fx[i].clock_interval_ms, 500, 600000);
                g_config.fx[i].ave5_button     = constrain(r["ave5_button"].as<int>(), 0, (int)AVE5_BUTTON_COUNT - 1);
                i++;
            }
        }

        JsonObject boot = p["boot"];
        if (!boot.isNull()) {
            g_config.boot.enabled        = boot["enabled"] | g_config.boot.enabled;
            g_config.boot.start_delay_ms = boot["start_delay_ms"] | g_config.boot.start_delay_ms;
            uint8_t sc = constrain(boot["step_count"].as<int>(), 0, BOOT_MAX_STEPS);
            JsonArray steps = boot["steps"].as<JsonArray>();
            int si = 0;
            for (JsonObject st : steps) {
                if (si >= sc) break;
                g_config.boot.steps[si].relay         = constrain(st["relay"].as<int>(), 0, RELAY_COUNT - 1);
                g_config.boot.steps[si].presses       = constrain(st["presses"].as<int>(), 1, 5);
                g_config.boot.steps[si].length_ms     = constrain(st["length_ms"].as<int>(), 30, 5000);
                g_config.boot.steps[si].gap_ms        = constrain(st["gap_ms"].as<int>(), 20, 5000);
                g_config.boot.steps[si].wait_after_ms = constrain(st["wait_after_ms"].as<int>(), 50, 60000);
                si++;
            }
            g_config.boot.step_count = sc;
        }
        extern void saveConfiguration();   // main.cpp
        saveConfiguration();
        respType = "saved";
    }
    else if (!strcmp(type, "boot_replay")) {
        extern BootSequencer g_boot;
        g_boot.start();
        return;
    }
    else if (!strcmp(type, "factory_reset")) {
        extern void requestFactoryReset();  // main.cpp
        requestFactoryReset();
        return;
    }

    JsonDocument ack;
    ack["type"] = respType;
    String out; serializeJson(ack, out);
    client->text(out);
}

/* ============================================================================
 * TELEMETRY
 * ============================================================================ */

void WebConsole::broadcastTelemetry(const TelemetryPacket& p) {
    uint32_t now = millis();
    if (now - _last_broadcast < _telemetry_interval_ms) return;
    _last_broadcast = now;

    if (_ws->count() == 0) return;

    JsonDocument doc;
    doc["type"] = "telemetry";
    JsonObject t = doc["payload"].to<JsonObject>();

    t["state"] = p.state;
    t["state_name"] = STATE_NAMES[p.state];
    t["distance_raw"] = p.distance_raw;
    t["distance_filtered"] = p.distance_filtered;
    t["mix_pwm"] = p.mix_pwm;
    t["pi_trigger"] = p.pi_trigger;
    t["base_pwm_f"] = p.base_pwm_f;
    t["gamma_shaped"] = p.gamma_shaped;
    t["peak_gate"] = p.peak_gate;
    t["biquad_raw"] = p.biquad_raw;
    t["agc_normalized"] = p.agc_normalized;
    t["timestamp_ms"] = p.timestamp_ms;

    JsonArray gates = t["stationary_energy"].to<JsonArray>();
    for (int i = 0; i < RADAR_GATE_COUNT; i++) gates.add(p.stationary_energy[i]);

    JsonArray rseq = t["relay_seq"].to<JsonArray>();
    JsonArray rprs = t["relay_pressed"].to<JsonArray>();
    for (int i = 0; i < RELAY_COUNT; i++) { rseq.add(p.relay_seq[i]); rprs.add(p.relay_pressed[i]); }

    JsonArray vval = t["vactrol_val"].to<JsonArray>();
    JsonArray vauto = t["vactrol_auto"].to<JsonArray>();
    for (int i = 0; i < VACTROL_COUNT; i++) { vval.add(p.vactrol_val[i]); vauto.add(p.vactrol_auto[i]); }

    String out; serializeJson(doc, out);
    _ws->textAll(out);
}

size_t WebConsole::getClientCount() { return _ws ? _ws->count() : 0; }
void WebConsole::update() {}
void WebConsole::setRelayFireHandler(void (*fn)(uint8_t)) { _relay_fire = fn; }
void WebConsole::setRelayStopHandler(void (*fn)(uint8_t)) { _relay_stop = fn; }
void WebConsole::_broadcastConfig() {}