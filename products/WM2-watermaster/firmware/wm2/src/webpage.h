#pragma once
// Built-in control page. Plain HTML/JS, no external files (works on SoftAP
// with no internet). Talks to the node over WebSocket /ws with the same
// command JSON as MQTT.
#include <Arduino.h>

static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WM2 node</title>
<style>
:root{--bg:#f4f6f3;--card:#fff;--ink:#1d2a1f;--q:#5f6b61;--line:#dde3dc;--acc:#2f7d4f;--warn:#b26b00;--bad:#b3261e;--blue:#1f5fa8}
@media (prefers-color-scheme:dark){:root{--bg:#121613;--card:#1b211c;--ink:#e6ece6;--q:#9aa79c;--line:#2c352e;--acc:#5cbf83;--warn:#e0a040;--bad:#ef6b62;--blue:#6aa8ea}}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--ink)}
header{padding:12px 16px;background:var(--card);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:2}
h1{font-size:17px;margin:0}small,.q{color:var(--q)}main{padding:12px 16px;max-width:860px;margin:auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px;margin-bottom:12px}
.card h2{font-size:15px;margin:0 0 8px}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}
.pill{display:inline-block;padding:2px 8px;border-radius:99px;font-size:12px;font-weight:600;border:1px solid var(--line)}
.on{background:var(--acc);color:#fff;border-color:var(--acc)}.warn{background:var(--warn);color:#fff;border-color:var(--warn)}.bad{background:var(--bad);color:#fff;border-color:var(--bad)}
button{font:inherit;padding:7px 12px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--ink);cursor:pointer}
button.p{background:var(--acc);border-color:var(--acc);color:#fff}button.d{border-color:var(--bad);color:var(--bad)}
input,select{font:inherit;padding:6px 8px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--ink);max-width:100%}
input[type=number]{width:80px}label{font-size:13px;color:var(--q)}
table{border-collapse:collapse;width:100%;font-size:13px}td,th{padding:4px 6px;border-bottom:1px solid var(--line);text-align:left}
nav{display:flex;gap:6px;overflow-x:auto;padding:8px 16px;background:var(--card);border-bottom:1px solid var(--line)}
nav button{white-space:nowrap}nav button.sel{background:var(--ink);color:var(--bg)}
.big{font-size:22px;font-weight:700}.hide{display:none}#toast{position:fixed;bottom:16px;left:16px;right:16px;max-width:520px;margin:auto;padding:10px 14px;border-radius:8px;background:var(--ink);color:var(--bg);display:none}
.days label{display:inline-flex;gap:3px;align-items:center;margin-right:6px;color:var(--ink)}
</style></head><body>
<header><h1 id="title">WM2 node</h1><small id="sub">connecting...</small></header>
<nav><button data-t="home" class="sel">Status</button><button data-t="prog">Programs</button><button data-t="hist">History</button><button data-t="set">Settings</button></nav>
<main>
<section id="t-home">
 <div class="card"><div class="row" style="justify-content:space-between">
  <div><div class="q">Water in pipe</div><div class="big" id="water">-</div><small id="ps"></small></div>
  <div><div class="q">Battery</div><div class="big" id="batt">-</div><small id="battsub"></small></div>
  <div><div class="q">Bypass</div><div class="row"><span id="byp" class="pill">off</span>
   <input id="bypmin" type="number" value="60" min="1" max="1440"> min <button onclick="bypass(true)">On</button><button onclick="bypass(false)">Off</button></div></div>
 </div></div>
 <div class="grid" id="valves"></div>
 <div class="card"><h2>Flow</h2><table><thead><tr><th>Input</th><th>Valve</th><th>L/min</th><th>Litres today</th></tr></thead><tbody id="flow"></tbody></table></div>
 <div class="card"><h2>Alarms</h2><div id="alarms" class="q">none</div></div>
 <div class="card"><h2>Recent events</h2><table><tbody id="events"></tbody></table><button onclick="loadEvents()">Refresh</button></div>
</section>
<section id="t-prog" class="hide"><div id="progs"></div></section>
<section id="t-hist" class="hide">
 <div class="card"><div class="row">
  <button onclick="hist('today')">Today</button><button onclick="hist('yday')">Yesterday</button><button onclick="hist('7d')">Last 7 days</button><button onclick="hist('30d')">Last 30 days</button><button onclick="hist('1y')">Last year</button></div>
  <div class="row" style="margin-top:8px"><input type="date" id="hfrom"> to <input type="date" id="hto"><button onclick="hist('range')">Show</button><a id="csv" href="#">Download CSV</a></div></div>
 <div class="card"><h2>Totals</h2><div id="htot" class="q">pick a period</div></div>
 <div class="card"><h2>Day results</h2><table><thead><tr><th>Date</th><th>Valve</th><th>Planned</th><th>Run</th><th>Litres</th><th>Status</th></tr></thead><tbody id="hdays"></tbody></table></div>
 <div class="card"><h2>Detail</h2><div style="overflow-x:auto"><table id="hpts"></table></div></div>
</section>
<section id="t-set" class="hide">
 <div class="card"><h2>Farm WiFi</h2><div class="row"><input id="ssid" placeholder="SSID"><input id="wpass" type="password" placeholder="password"><button onclick="scan()">Scan</button><button class="p" onclick="saveWifi()">Save</button></div><div id="scanres" class="q"></div></div>
 <div class="card"><h2>MQTT broker</h2>
  <div class="row"><label>Use</label><select id="broker" onchange="brokerVis()"><option value="agrisense">AgriSense</option><option value="girish">Girish (own broker)</option></select><span id="mqst" class="q"></span></div>
  <div id="b-ag" class="grid" style="margin-top:8px">
   <div><label>AgriSense host</label><input id="ahost"></div><div><label>Port</label><input id="aport" type="number"></div>
   <div><label>User</label><input id="auser"></div><div><label>Password</label><input id="apass" type="password" placeholder="unchanged"></div>
   <div><label>Device ID (topic agrisense/WM2/ID/)</label><input id="aid"></div></div>
  <div id="b-gi" class="grid" style="margin-top:8px">
   <div><label>Girish host</label><input id="mhost"></div><div><label>Port</label><input id="mport" type="number"></div>
   <div><label>User</label><input id="muser"></div><div><label>Password</label><input id="mpass" type="password" placeholder="unchanged"></div>
   <div><label>Farm</label><input id="farm"></div><div><label>Zone</label><input id="zone"></div><div><label>Node</label><input id="node"></div></div>
  <button class="p" onclick="saveMqtt()" style="margin-top:8px">Save</button></div>
 <div class="card"><h2>Reporting</h2><div class="grid">
  <div><label>Active interval (s, 30-1800)</label><input id="act" type="number"></div>
  <div><label>Heartbeat (s, 60-14400)</label><input id="hb" type="number"></div>
  <div><label>Idle delay (s)</label><input id="idl" type="number"></div></div>
  <button class="p" onclick="saveReport()" style="margin-top:8px">Save</button></div>
 <div class="card"><h2>Valves and sensors</h2><div id="hwset"></div><button class="p" onclick="saveHw()">Save</button></div>
 <div class="card"><h2>Options</h2><div class="grid">
  <div><label>V1/V2</label><select id="seq"><option value="1">One after the other</option><option value="0">Together</option></select></div>
  <div><label>Pressure settle (s)</label><input id="settle" type="number" step="0.5"></div>
  <div><label>Battery Ah</label><input id="cap" type="number"></div>
  <div><label>Chemistry</label><select id="chem"><option>lifepo4</option><option>lead-acid</option></select></div></div>
  <button class="p" onclick="saveOpts()" style="margin-top:8px">Save</button></div>
 <div class="card"><h2>Device</h2><div id="dev" class="q"></div><div class="row" style="margin-top:8px">
  <button onclick="cmd({cmd:'rtc_sync',epoch:Math.floor(Date.now()/1000)}).then(()=>toast('Clock set from phone'))">Set clock from phone</button>
  <button onclick="cmd({cmd:'reboot'})">Reboot</button><button class="d" onclick="if(confirm('Erase all history?'))cmd({cmd:'clear_history'})">Clear history</button></div></div>
</section>
</main><div id="toast"></div>
<script>
const $=id=>document.getElementById(id);let S=null,C=null,ws=null,pend={},nid=1;
const DAYS=['mon','tue','wed','thu','fri','sat','sun'];
function toast(t){const e=$('toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',3500)}
function conn(){ws=new WebSocket('ws://'+location.host+'/ws');ws.onmessage=m=>{const j=JSON.parse(m.data);
 if(j.type==='state'){S=j.data;render();if(!S.time_ok&&!window._ts){window._ts=1;cmd({cmd:'rtc_sync',epoch:Math.floor(Date.now()/1000)}).then(()=>toast('Clock set from phone'))}}else if(j.type==='event'){addEvent(j.data)}else if(j.id&&pend[j.id]){pend[j.id](j);delete pend[j.id]}};
 ws.onclose=()=>{$('sub').textContent='disconnected, retrying...';setTimeout(conn,2000)};ws.onopen=()=>{init()}}
function cmd(o){return new Promise(r=>{o.id='w'+(nid++);o.src='web';pend[o.id]=j=>{if(!j.ok)toast('Error: '+j.err);r(j)};
 if(ws&&ws.readyState===1)ws.send(JSON.stringify(o));else fetch('/api/cmd',{method:'POST',body:JSON.stringify(o)}).then(x=>x.json()).then(j=>pend[o.id]&&pend[o.id](j))})}
function fmtT(ts){if(!ts)return'-';const d=new Date(ts*1000);return d.toLocaleString()}
function render(){if(!S)return;$('title').textContent='WM2 '+S.node;
 $('sub').textContent=(S.time_ok?fmtT(S.ts):'clock not set')+' | '+(S.wifi.ip?('WiFi '+S.wifi.rssi+' dBm'):'no WiFi')+(S.wifi.ap?' | SoftAP on':'')+' | MQTT '+(S.mqtt?'ok':'off')+' | '+S.state;
 $('water').innerHTML=S.water?'<span class="pill on">YES</span>':'<span class="pill">no</span>';
 $('ps').textContent='pressure switch '+(S.ps?'ON':'off');
 $('batt').textContent=S.batt.v?S.batt.v.toFixed(2)+' V':'no 12V';$('battsub').textContent=S.batt.v?(S.batt.soc+'% | '+S.batt.trend+' | ~'+S.batt.days.toFixed(0)+' days | '+S.batt.level):'running on USB';
 const b=$('byp');b.textContent=S.bypass.on?('ON '+Math.ceil(S.bypass.left_s/60)+' min'):'off';b.className='pill'+(S.bypass.on?' warn':'');
 const ae=document.activeElement;if(ae&&ae.tagName==='INPUT'&&$('valves').contains(ae))return renderRest();
 const keep={};[1,2].forEach(i=>{const e=$('om'+i);if(e)keep[i]=e.value});
 $('valves').innerHTML=S.valves.map(v=>{const st=v.pending?'<span class="pill warn">pulse pending (12V?)</span>':v.open?'<span class="pill on">OPEN</span>':v.waiting?'<span class="pill warn">waiting for water</span>':'<span class="pill">closed</span>';
  const pr=v.mode==='program'?(v.due?('today '+v.run_min+' / '+v.planned_min+' min'):v.done?'done for today':'not due today'):v.mode;
  return `<div class="card"><h2>Valve ${v.v} ${st}</h2><div class="q">mode ${v.mode} | ${pr}${v.manual?' | manual '+(v.manual_left_s?Math.ceil(v.manual_left_s/60)+' min left':'on'):''}</div>
  <div class="q">litres today ${v.litres.toFixed(1)}</div><div class="row" style="margin-top:8px"><input id="om${v.v}" type="number" value="10" min="1"> min
  <button class="p" onclick="openV(${v.v})">Open</button><button onclick="cmd({cmd:'valve_close',valve:${v.v}})">Close</button></div></div>`}).join('');
 [1,2].forEach(i=>{if(keep[i]!==undefined)$('om'+i).value=keep[i]});renderRest()}
function renderRest(){
 $('flow').innerHTML=S.flow.map(f=>`<tr><td>Flow ${f.in}</td><td>${f.valve?'V'+f.valve:'-'}</td><td>${f.lpm.toFixed(1)}</td><td>${f.l_today.toFixed(1)}</td></tr>`).join('');
 $('alarms').innerHTML=S.alarms.length?S.alarms.map(a=>'<span class="pill bad">'+a+'</span>').join(' '):'none'}
function openV(v){cmd({cmd:'valve_open',valve:v,minutes:+$('om'+v).value}).then(j=>j.ok&&toast(j.data&&j.data.state==='waiting_for_water'?'Waiting for water':'Opening V'+v))}
function bypass(on){cmd({cmd:'bypass',on:on,minutes:+$('bypmin').value})}
function addEvent(e){const tb=$('events');const r=document.createElement('tr');r.innerHTML=`<td>${fmtT(e.ts)}</td><td>${evText(e)}</td>`;tb.prepend(r);while(tb.rows.length>30)tb.deleteRow(30)}
function evText(e){switch(e.event){case'valve':return'V'+e.valve+(e.open?' opened':' closed')+' ('+e.reason+(e.src?', '+e.src:'')+')';case'water':return e.present?'Water arrived':'Water gone';
 case'alarm':return'Alarm '+e.code+(e.active?' ON':' cleared')+(e.valve?' V'+e.valve:'');case'day_result':return'Day result V'+e.valve+': '+e.status+', '+e.run_min+'/'+(e.planned_min||0)+' min';case'bypass':return'Bypass '+(e.on?'on':'off');default:return e.event}}
function loadEvents(){cmd({cmd:'get_events',limit:30}).then(j=>{$('events').innerHTML='';(j.data.events||[]).forEach(addEvent)})}
function progCard(v){const p=v.program||{};const days=p.days||[];const t=p.type||'timer';const w=p.window||['00:00','23:59'];
 return `<div class="card"><h2>Valve ${v.valve} program</h2><div class="row"><label>Mode</label><select id="pm${v.valve}">${['program','manual','off'].map(m=>`<option ${m===v.mode?'selected':''}>${m}</option>`).join('')}</select>
 <label><input type="checkbox" id="pe${v.valve}" ${p.enabled?'checked':''}> enabled</label></div>
 <div class="days" style="margin:8px 0">${DAYS.map(d=>`<label><input type="checkbox" id="pd${v.valve}${d}" ${days.includes(d)?'checked':''}>${d}</label>`).join('')}</div>
 <div class="row"><select id="pt${v.valve}"><option value="timer" ${t==='timer'?'selected':''}>minutes</option><option value="litres" ${t==='litres'?'selected':''}>litres</option></select>
 <input id="pa${v.valve}" type="number" value="${t==='litres'?(p.litres||100):(p.minutes||30)}"> per day, window <input id="pw1${v.valve}" type="time" value="${w[0]}"> to <input id="pw2${v.valve}" type="time" value="${w[1]}">
 <label><input type="checkbox" id="pc${v.valve}" ${p.carry_over?'checked':''}> carry unfinished to next day</label></div>
 <div class="row" style="margin-top:8px"><button class="p" onclick="saveProg(${v.valve})">Save program</button><button onclick="cmd({cmd:'set_mode',valve:${v.valve},mode:$('pm${v.valve}').value})">Set mode only</button></div></div>`}
function saveProg(v){const t=$('pt'+v).value;const p={enabled:$('pe'+v).checked,type:t,days:DAYS.filter(d=>$('pd'+v+d).checked),window:[$('pw1'+v).value,$('pw2'+v).value],carry_over:$('pc'+v).checked};
 if(t==='litres')p.litres=+$('pa'+v).value;else p.minutes=+$('pa'+v).value;
 cmd({cmd:'set_program',valve:v,program:p,set_mode:false}).then(j=>{if(j.ok)cmd({cmd:'set_mode',valve:v,mode:$('pm'+v).value}).then(()=>toast('Saved V'+v))})}
function loadProgs(){cmd({cmd:'get_program'}).then(j=>{$('progs').innerHTML=j.data.programs.map(progCard).join('')})}
function dayStart(d){const x=new Date(d);x.setHours(0,0,0,0);return Math.floor(x/1000)}
function hist(k){const now=Math.floor(Date.now()/1000);let f,t=now,res='';const today=dayStart(new Date());
 if(k==='today')f=today;else if(k==='yday'){f=today-86400;t=today-1}else if(k==='7d'){f=today-6*86400;res='hour'}else if(k==='30d'){f=today-29*86400;res='day'}else if(k==='1y'){f=today-364*86400;res='day'}
 else{if(!$('hfrom').value||!$('hto').value)return toast('Pick both dates');f=dayStart($('hfrom').value+'T00:00');t=dayStart($('hto').value+'T00:00')+86399}
 $('csv').href='/api/history.csv?from='+f+'&to='+t;
 cmd({cmd:'get_history',from:f,to:t,res:res}).then(j=>{const d=j.data,T=d.totals;
  $('htot').innerHTML=`V1 ${T.valve_litres[0]} L, open ${T.valve_open_min[0]} min | V2 ${T.valve_litres[1]} L, open ${T.valve_open_min[1]} min<br>water in pipe ${T.water_min} min | lowest battery ${T.min_batt} V | ${T.records} records`;
  const cols=d.columns.split(',');$('hpts').innerHTML='<tr>'+cols.map(c=>'<th>'+c+'</th>').join('')+'</tr>'+d.points.slice(-300).reverse().map(p=>'<tr>'+p.map((x,i)=>'<td>'+(i===0?fmtT(x):x)+'</td>').join('')+'</tr>').join('')});
 cmd({cmd:'get_days',days:Math.max(2,Math.ceil((now-f)/86400)+1)}).then(j=>{$('hdays').innerHTML=(j.data.days||[]).reverse().map(r=>`<tr><td>${r.date}</td><td>V${r.valve}</td><td>${r.planned_min!=null?r.planned_min+' min':r.planned_l+' L'}</td><td>${r.run_min} min</td><td>${(r.litres||[]).join(' + ')}</td><td>${r.status}</td></tr>`).join('')})}
function loadCfg(){cmd({cmd:'get_config'}).then(j=>{C=j.data;$('ssid').value=C.ssid;$('mhost').value=C.mqtt_host;$('mport').value=C.mqtt_port;$('muser').value=C.mqtt_user;
 $('broker').value=C.broker;$('ahost').value=C.ag_host;$('aport').value=C.ag_port;$('auser').value=C.ag_user;$('aid').value=C.ag_id;brokerVis();
 $('farm').value=C.farm;$('zone').value=C.zone;$('node').value=C.node;$('act').value=C.active_s;$('hb').value=C.heartbeat_s;$('idl').value=C.idle_delay_s;
 $('seq').value=C.sequential?'1':'0';$('settle').value=C.ps_settle_s;$('cap').value=C.capacity_ah;$('chem').value=C.chemistry;
 $('hwset').innerHTML='<table><tr><th>Valve</th><th>Pulse ms</th><th>Invert</th><th>Never close</th><th>Water lost</th><th>Close after min</th></tr>'+C.valves.map(v=>`<tr><td>V${v.valve}</td><td><input id="hp${v.valve}" type="number" value="${v.pulse_ms}"></td><td><input type="checkbox" id="hi${v.valve}" ${v.invert?'checked':''}></td><td><input type="checkbox" id="hn${v.valve}" ${v.never_close?'checked':''}></td><td><select id="hw${v.valve}"><option ${v.on_water_loss==='keep'?'selected':''}>keep</option><option ${v.on_water_loss==='close'?'selected':''}>close</option></select></td><td><input id="hc${v.valve}" type="number" value="${v.close_after_min}"></td></tr>`).join('')+'</table>'
 +'<table style="margin-top:8px"><tr><th>Flow</th><th>Valve (0 = unused)</th><th>Pulses per litre</th></tr>'+C.flow.map(f=>`<tr><td>${f.input}</td><td><input id="fv${f.input}" type="number" min="0" max="2" value="${f.valve}"></td><td><input id="fp${f.input}" type="number" step="0.1" value="${f.ppl}"></td></tr>`).join('')+'</table>'});
 cmd({cmd:'device_info'}).then(j=>{const d=j.data;$('mqst').textContent=` active: ${d.broker} ${d.broker_host||''} (${d.mqtt?'connected':'not connected'})`;$('dev').textContent=`fw ${d.fw} | ${d.chip} | ${d.mac} | ${d.host} | ${d.ip||'no IP'} | SoftAP ${d.ap?d.ap_ssid:'off'} | storage ${Math.round(d.fs_used/1024)}/${Math.round(d.fs_total/1024)} KB | heap ${d.heap}`})}
function scan(){$('scanres').textContent='scanning...';cmd({cmd:'wifi_scan'}).then(j=>{$('scanres').innerHTML=(j.data.networks||[]).map(n=>`<a href="#" onclick="$('ssid').value='${n.ssid}';return false">${n.ssid}</a> (${n.rssi})`).join(', ')})}
function saveWifi(){cmd({cmd:'wifi_config',ssid:$('ssid').value,pass:$('wpass').value}).then(j=>j.ok&&toast('Saved, joining '+$('ssid').value))}
function brokerVis(){const ag=$('broker').value==='agrisense';$('b-ag').classList.toggle('hide',!ag);$('b-gi').classList.toggle('hide',ag)}
function saveMqtt(){const ag=$('broker').value==='agrisense';const o=ag?{cmd:'mqtt_config',broker:'agrisense',host:$('ahost').value,port:+$('aport').value,user:$('auser').value,ag_id:$('aid').value}
 :{cmd:'mqtt_config',broker:'girish',host:$('mhost').value,port:+$('mport').value,user:$('muser').value,farm:$('farm').value,zone:$('zone').value,node:$('node').value};
 const pw=ag?$('apass').value:$('mpass').value;if(pw)o.pass=pw;cmd(o).then(j=>j.ok&&toast('Saved: '+j.data.broker+' broker, topics '+j.data.base))}
function saveReport(){cmd({cmd:'set_report',active_s:+$('act').value,heartbeat_s:+$('hb').value,idle_delay_s:+$('idl').value}).then(j=>j.ok&&toast('Saved'))}
function saveOpts(){cmd({cmd:'set_options',sequential:$('seq').value==='1',ps_settle_s:+$('settle').value}).then(()=>cmd({cmd:'set_battery',capacity_ah:+$('cap').value,chemistry:$('chem').value})).then(j=>j.ok&&toast('Saved'))}
function saveHw(){const valves=[1,2].map(v=>({valve:v,pulse_ms:+$('hp'+v).value,invert:$('hi'+v).checked,never_close:$('hn'+v).checked,on_water_loss:$('hw'+v).value,close_after_min:+$('hc'+v).value}));
 const flow=[1,2,3,4].map(i=>({input:i,valve:+$('fv'+i).value,ppl:+$('fp'+i).value}));cmd({cmd:'set_config',config:{valves:valves,flow:flow}}).then(j=>j.ok&&toast('Saved'))}
function init(){loadEvents()}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button').forEach(x=>x.classList.remove('sel'));b.classList.add('sel');
 ['home','prog','hist','set'].forEach(t=>$('t-'+t).classList.toggle('hide',t!==b.dataset.t));if(b.dataset.t==='prog')loadProgs();if(b.dataset.t==='set')loadCfg();if(b.dataset.t==='hist'&&!$('hpts').innerHTML)hist('today')});
conn();
</script></body></html>)HTML";
