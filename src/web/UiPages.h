#pragma once

#include <Arduino.h>

/**
 * Laborwerkzeug-UI: Graphit mit Bernstein-Akzent, in der Familienlinie der
 * anderen Projekte. Kein Chart, keine Bibliothek.
 *
 * Zwei Dinge sind hier nicht verhandelbar:
 *   - Der Not-Stop ist fix sichtbar, nicht in einem Tab versteckt.
 *   - Der Deadman-Countdown ist immer im Blick, sobald er scharf ist.
 */
static const char PAGE_MAIN[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FTMS-Probe</title>
<style>
:root{--bg:#15171a;--bg2:#1c1f24;--bg3:#24282f;--bd:#31363f;--tx:#e6e8ea;--mu:#9aa1ab;
--am:#f0a13a;--ok:#4ec9a5;--no:#e3576b;--mono:ui-monospace,"IBM Plex Mono",Consolas,monospace}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:14px/1.5 system-ui,-apple-system,sans-serif}
header{position:sticky;top:0;z-index:10;background:var(--bg2);border-bottom:1px solid var(--bd);
padding:10px 14px;display:flex;gap:12px;align-items:center;flex-wrap:wrap}
h1{font-size:16px;margin:0;letter-spacing:.5px}
h1 small{color:var(--mu);font-weight:400;font-size:12px;font-family:var(--mono)}
.grow{flex:1}
.pill{font-family:var(--mono);font-size:12px;padding:2px 8px;border-radius:10px;background:var(--bg3);
border:1px solid var(--bd);white-space:nowrap}
.pill.ok{color:var(--ok);border-color:#2f5b4e}.pill.no{color:var(--no);border-color:#5b2f38}
.pill.am{color:var(--am);border-color:#5b4726}
button{background:var(--bg3);color:var(--tx);border:1px solid var(--bd);border-radius:6px;
padding:7px 12px;font-size:13px;cursor:pointer;font-family:inherit}
button:hover{border-color:var(--am)}button:disabled{opacity:.45;cursor:not-allowed}
button.p{background:var(--am);color:#1b1206;border-color:var(--am);font-weight:600}
#stop{background:var(--no);border-color:var(--no);color:#fff;font-weight:700;padding:9px 18px}
nav{display:flex;gap:4px;padding:8px 14px;background:var(--bg);border-bottom:1px solid var(--bd);
overflow-x:auto}
nav button{border-color:transparent;background:transparent;color:var(--mu)}
nav button.on{color:var(--am);border-color:var(--bd);background:var(--bg2)}
main{padding:14px;max-width:1100px}
.card{background:var(--bg2);border:1px solid var(--bd);border-radius:8px;padding:12px;margin-bottom:12px}
.card h2{font-size:13px;margin:0 0 10px;color:var(--mu);text-transform:uppercase;letter-spacing:1px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
table{width:100%;border-collapse:collapse;font-size:13px}
th{text-align:left;color:var(--mu);font-weight:500;font-size:11px;text-transform:uppercase;
letter-spacing:.5px;padding:4px 6px;border-bottom:1px solid var(--bd)}
td{padding:5px 6px;border-bottom:1px solid #23262b;vertical-align:top}
.mono{font-family:var(--mono);font-size:12px}
input,select{background:var(--bg);border:1px solid var(--bd);border-radius:6px;padding:6px 9px;
color:var(--tx);font-family:var(--mono);font-size:13px}
input:focus,select:focus{outline:none;border-color:var(--am)}
.hint{color:var(--mu);font-size:12px}
.svc{border:1px solid var(--bd);border-radius:6px;margin-bottom:8px;overflow:hidden}
.svc>div:first-child{background:var(--bg3);padding:7px 10px;font-family:var(--mono);font-size:12px}
.vendor{color:var(--am)}
#log{font-family:var(--mono);font-size:11.5px;background:#0f1114;border:1px solid var(--bd);
border-radius:6px;padding:8px;height:60vh;overflow:auto;white-space:pre-wrap;word-break:break-all}
.d-notify{color:var(--ok)}.d-write{color:var(--am)}.d-resp{color:#7fb2ff}
.d-error{color:var(--no)}.d-guard{color:var(--no)}.d-phase{color:#c58af9}
.d-info,.d-adv,.d-read,.d-indicate{color:var(--mu)}
</style></head><body>
<header>
  <h1>FTMS-PROBE <small id="ver"></small></h1>
  <span class="pill" id="p-state">--</span>
  <span class="pill" id="p-links">0 Links</span>
  <span class="pill" id="p-dead">Deadman aus</span>
  <span class="pill" id="p-log">seq 0</span>
  <span class="grow"></span>
  <span class="pill" id="p-net">--</span>
  <button id="stop">NOT-STOP</button>
</header>
<nav>
  <button data-t="live" class="on">Live</button>
  <button data-t="scan">Scan</button>
  <button data-t="gatt">GATT</button>
  <button data-t="ctrl">Control Point</button>
  <button data-t="log">Log</button>
  <button data-t="cfg">Config</button>
  <button data-t="ota">OTA</button>
</nav>
<main>

<section id="t-live">
  <div class="card"><h2>Live Indoor Bike (0x2AD2)</h2>
    <div class="row" style="gap:16px;font-family:var(--mono);font-size:18px">
      <div><span class="hint">Power</span><div id="lv-p">— W</div></div>
      <div><span class="hint">Cadence</span><div id="lv-c">— rpm</div></div>
      <div><span class="hint">Speed</span><div id="lv-s">— km/h</div></div>
      <div><span class="hint">HR</span><div id="lv-h">—</div></div>
      <div><span class="hint">Dist</span><div id="lv-d">— m</div></div>
      <div><span class="hint">t</span><div id="lv-t">— s</div></div>
    </div>
    <div class="row" style="margin-top:10px">
      <button class="p" onclick="post('/api/probe/reconnect')">Reconnect Bike</button>
      <button onclick="setupLive()">Notify 2AD2+2AD9</button>
      <button onclick="post('/api/probe/disconnect',{all:true})">Trennen</button>
      <a href="/api/probe/summary" target="_blank"><button>Summary JSON</button></a>
      <a href="/api/probe/export" download="probe-export.ndjson"><button>Export</button></a>
      <span class="hint" id="live-info"></span>
    </div>
  </div>
</section>

<section id="t-scan" hidden>
  <div class="card"><h2>Scan</h2>
    <div class="row">
      <button class="p" onclick="scanStart()">Scan starten</button>
      <button onclick="post('/api/probe/scan/stop')">Stop</button>
      <button onclick="loadDevices()">Liste neu laden</button>
      <span class="hint" id="scan-info"></span>
    </div>
    <p class="hint">Scan ist waehrend offener Links standardmaessig gesperrt (Config).</p>
  </div>
  <div class="card"><h2>Gefundene Geraete</h2>
    <table><thead><tr><th>Name</th><th>MAC</th><th>RSSI</th><th>Beworbene Services</th><th></th></tr></thead>
    <tbody id="devs"><tr><td colspan="5" class="hint">Noch nichts gescannt.</td></tr></tbody></table>
  </div>
</section>

<section id="t-gatt" hidden>
  <div class="card"><h2>Verbundene Links</h2>
    <div id="links" class="hint">Kein Link offen.</div>
  </div>
  <div class="card"><h2>Attribute</h2>
    <div class="row"><button onclick="loadGatt()">GATT lesen</button>
      <span class="hint" id="gatt-info"></span></div>
    <div id="gatt" style="margin-top:10px"></div>
  </div>
</section>

<section id="t-ctrl" hidden>
  <div class="card"><h2>Schritt 4 aus BLE-SCAN.md</h2>
    <p class="hint">Dieses Bike: Control Point per <b>Notify</b> (kein Indicate).
      Deadman-Labor-Modus arm't erst bei Last-Opcodes (04/05/11).</p>
    <div class="row">
      <button class="p" onclick="sub('2AD9','notify')">1. Notify 2AD9 an</button>
      <button onclick="sub('2AD2','notify')">Notify 2AD2 an</button>
      <button onclick="sub('2ADA','notify')">Notify 2ADA an</button>
    </div>
    <div class="row" style="margin-top:8px">
      <button class="p" onclick="cp('00')">2. Request Control (00)</button>
      <button onclick="cp('07')">3. Start (07)</button>
      <button onclick="cp('05 64 00')">4. 100 W (05 64 00)</button>
      <button onclick="cp('04 0A')">5. Stufe 10 (04 0A)</button>
      <button onclick="cp('04 64 00')">5b. Stufe 10,0 (04 64 00)</button>
      <button onclick="cp('08 01')">6. Stop (08 01)</button>
    </div>
    <div class="row" style="margin-top:8px">
      <input id="cp-hex" placeholder="freies Hex, z.B. 05 96 00" size="22">
      <button onclick="cp(document.getElementById('cp-hex').value)">Senden</button>
    </div>
  </div>
  <div class="card"><h2>Antworten</h2>
    <div id="cp-out" class="mono hint">Noch nichts gesendet.</div>
  </div>
  <div class="card"><h2>Phase</h2>
    <div class="row">
      <span class="hint">markiert die folgenden Logeintraege</span>
      <button onclick="phase('idle')">idle</button>
      <button onclick="phase('pedaling')">pedaling</button>
      <input id="ph" placeholder="eigene Phase" size="16">
      <button onclick="phase(document.getElementById('ph').value)">setzen</button>
    </div>
  </div>
</section>

<section id="t-log" hidden>
  <div class="card"><h2>Rohbyte-Log</h2>
    <div class="row">
      <label class="hint"><input type="checkbox" id="tail" checked> mitlaufen</label>
      <button onclick="post('/api/probe/log/clear').then(()=>{logSeq=0;document.getElementById('log').textContent=''})">Leeren</button>
      <a href="/api/probe/log?since=0&amp;max=768" download="probe-log.jsonl"><button>Als JSONL laden</button></a>
      <span class="hint" id="log-info"></span>
    </div>
    <div id="log" style="margin-top:10px"></div>
  </div>
</section>

<section id="t-cfg" hidden>
  <div class="card"><h2>Safety-Limiter</h2>
    <div class="row"><label class="hint">Control-Writes erlauben</label>
      <input type="checkbox" id="c-ctrl"></div>
    <div class="row" style="margin-top:6px"><label class="hint">max. Watt (0x05)</label>
      <input id="c-watt" size="5"></div>
    <div class="row" style="margin-top:6px"><label class="hint">max. Stufe (0x04)</label>
      <input id="c-level" size="5"></div>
    <div class="row" style="margin-top:6px"><label class="hint">Deadman [s], 0 = aus</label>
      <input id="c-dead" size="5"></div>
    <div class="row" style="margin-top:6px"><label class="hint">Deadman-Modus</label>
      <select id="c-dmode"><option value="lab">lab (nur Last)</option>
        <option value="safe">safe (jeder Write)</option>
        <option value="off">off</option></select></div>
    <div class="row" style="margin-top:6px"><label class="hint">0x11 Simulation erlauben</label>
      <input type="checkbox" id="c-sim"></div>
    <div class="row" style="margin-top:6px"><label class="hint">Auto-Reconnect</label>
      <input type="checkbox" id="c-arc"></div>
    <div class="row" style="margin-top:6px"><label class="hint">Scan waehrend Link</label>
      <input type="checkbox" id="c-swl"></div>
  </div>
  <div class="card"><h2>Hub / Geraet</h2>
    <div class="row"><label class="hint">Host</label><input id="c-host" size="16">
      <label class="hint">Port</label><input id="c-port" size="6"></div>
    <div class="row" style="margin-top:6px"><label class="hint">Name</label><input id="c-name" size="18"></div>
    <div class="row" style="margin-top:6px"><label class="hint">Bike-MAC</label><input id="c-bike" size="18">
      <label class="hint">Name</label><input id="c-biken" size="12"></div>
  </div>
  <div class="card"><div class="row">
    <button class="p" onclick="saveCfg()">Speichern</button>
    <button onclick="if(confirm('Neustart?'))post('/api/system/restart')">Neustart</button>
    <span class="hint" id="cfg-msg"></span>
  </div></div>
</section>

<section id="t-ota" hidden>
  <div class="card"><h2>Firmware</h2>
    <p class="hint">Alternativ per Hub: Bin nach /api/firmware-upload, dann /api/ota-push.</p>
    <div class="row"><input type="file" id="fw" accept=".bin">
      <button class="p" onclick="ota()">Hochladen</button>
      <span class="hint" id="ota-msg"></span></div>
  </div>
  <div class="card"><h2>Crash-Test (§14)</h2>
    <p class="hint">Reisst den Link ohne Stop-Kommando ab, um zu sehen, was das Bike
      mit einem gesetzten Ziel macht. Log vorher als JSONL sichern, es liegt im RAM.</p>
    <div class="row"><button onclick="crash()">Neustart ohne Not-Stop</button></div>
  </div>
</section>

</main>
<script>
var S={},logSeq=0,curLink=null;

function api(p,b){var o=b?{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify(b)}:{method:'POST'};return fetch(p,o).then(function(r){
  return r.json().catch(function(){return{ok:r.ok}})})}
function post(p,b){return api(p,b)}
function get(p){return fetch(p).then(function(r){return r.json()})}
function esc(s){return String(s==null?'':s).replace(/[<>&]/g,function(c){
  return{'<':'&lt;','>':'&gt;','&':'&amp;'}[c]})}
// fuer Strings, die in einem inline-onclick landen
function jsq(s){return String(s==null?'':s).replace(/\\/g,'').replace(/'/g,'')}
function toast(id,m,bad){var e=document.getElementById(id);if(!e)return;
  e.textContent=m;e.style.color=bad?'var(--no)':'var(--mu)'}

// ── Tabs ───────────────────────────────────────────────
document.querySelectorAll('nav button').forEach(function(b){
  b.onclick=function(){
    document.querySelectorAll('nav button').forEach(function(x){x.classList.remove('on')});
    b.classList.add('on');
    ['live','scan','gatt','ctrl','log','cfg','ota'].forEach(function(t){
      document.getElementById('t-'+t).hidden=(t!==b.dataset.t)});
    if(b.dataset.t==='cfg')loadCfg();
    if(b.dataset.t==='gatt')loadGatt();
  }});

// ── Status ─────────────────────────────────────────────
function renderStatus(s){
  S=s;
  document.getElementById('ver').textContent='v'+s.version+' · '+s.boardLabel+' · '+s.ip;
  var p=s.probe||{},g=p.guard||{},l=s.log||{},lv=p.live||{};
  var st=document.getElementById('p-state');
  st.textContent=p.state||'?';
  st.className='pill '+(p.state==='LINKED'?'ok':(p.state==='PANIC'?'no':''));
  document.getElementById('p-links').textContent=(p.linkCount||0)+'/'+(p.maxLinks||0)+' Links';
  var d=document.getElementById('p-dead');
  var mode=g.deadmanMode||'lab';
  if(g.armed){d.className='pill am';d.textContent='Deadman '+Math.round((g.remainingMs||0)/1000)+' s ('+mode+')'}
  else{d.className='pill';d.textContent='Deadman aus ('+mode+')'}
  document.getElementById('p-log').textContent='seq '+(l.lastSeq||0)+(l.dropped?' ('+l.dropped+' verworfen)':'');
  var n=document.getElementById('p-net');
  n.className='pill '+(s.hubOk?'ok':'no');
  n.textContent=(s.hubOk?'Hub ok':'Hub still')+' · '+Math.round(s.heap/1024)+' kB · '+s.uptime;
  if(p.lastDisconnect) document.getElementById('live-info').textContent=
    'last disconnect: '+p.lastDisconnect+(p.lastDisconnectAgoMs!=null?' ('+Math.round(p.lastDisconnectAgoMs/1000)+'s)':'');
  renderLive(lv);
  renderLinks(p.links||[]);
}
function renderLive(lv){
  if(!lv||!lv.valid){
    ['lv-p','lv-c','lv-s','lv-h','lv-d','lv-t'].forEach(function(id){
      var el=document.getElementById(id); if(el && el.textContent.indexOf('—')<0 && !lv) return;
    });
    return;
  }
  document.getElementById('lv-p').textContent=(lv.power!=null?lv.power:'—')+' W';
  document.getElementById('lv-c').textContent=(lv.cadence!=null?Number(lv.cadence).toFixed(0):'—')+' rpm';
  document.getElementById('lv-s').textContent=(lv.speed!=null?Number(lv.speed).toFixed(1):'—')+' km/h';
  document.getElementById('lv-h').textContent=lv.heartRate!=null?lv.heartRate:'—';
  document.getElementById('lv-d').textContent=(lv.distance!=null?lv.distance:'—')+' m';
  document.getElementById('lv-t').textContent=(lv.elapsedS!=null?lv.elapsedS:'—')+' s';
}
function setupLive(){
  if(curLink===null && (S.probe&&S.probe.links&&S.probe.links[0])) curLink=S.probe.links[0].link;
  if(curLink===null){toast('live-info','kein Link',1);return}
  Promise.all([
    post('/api/probe/subscribe',{link:curLink,uuid:'2AD2',mode:'notify',enable:true}),
    post('/api/probe/subscribe',{link:curLink,uuid:'2AD9',mode:'notify',enable:true})
  ]).then(function(){toast('live-info','2AD2+2AD9 Notify aktiv')})
}
function renderLinks(links){
  var e=document.getElementById('links');
  if(!links.length){e.innerHTML='<span class="hint">Kein Link offen.</span>';curLink=null;return}
  if(curLink===null)curLink=links[0].link;
  var h='<table><thead><tr><th>#</th><th>Name</th><th>MAC</th><th>RSSI</th><th>Services</th>'
    +'<th>Notifies</th><th>Abos</th><th></th></tr></thead><tbody>';
  links.forEach(function(l){
    h+='<tr><td>'+l.link+'</td><td>'+esc(l.name||'-')+'</td><td class="mono">'+esc(l.mac)+'</td>'
     +'<td>'+l.rssi+'</td><td>'+l.services+'</td><td>'+l.notifies+'</td><td class="mono">'
     +(l.subs||[]).map(function(s){return s.uuid+':'+s.packets}).join(' ')+'</td>'
     +'<td><button onclick="curLink='+l.link+';loadGatt()">GATT</button> '
     +'<button onclick="post(\'/api/probe/disconnect\',{link:'+l.link+'})">Trennen</button></td></tr>';
  });
  e.innerHTML=h+'</tbody></table>';
}

// ── Scan ───────────────────────────────────────────────
function scanStart(){post('/api/probe/scan/start',{clear:true}).then(function(){
  setTimeout(loadDevices,1500)})}
function loadDevices(){get('/api/probe/devices').then(function(d){
  toast('scan-info',(d.count||0)+' Geraete, Scan '+(d.scanning?'laeuft':'aus'));
  var b=document.getElementById('devs');
  if(!d.devices||!d.devices.length){b.innerHTML='<tr><td colspan="5" class="hint">Nichts gefunden.</td></tr>';return}
  d.devices.sort(function(a,c){return c.rssi-a.rssi});
  b.innerHTML=d.devices.map(function(x){
    var sv=(x.services||[]).map(function(s){
      return '<span class="'+(s.vendor?'vendor':'')+'">'+s.uuid+(s.label?' ('+esc(s.label)+')':'')+'</span>'
    }).join('<br>')||'<span class="hint">keine</span>';
    return '<tr><td>'+esc(x.name||'(ohne Namen)')+(x.ftms?' <span class="pill ok">FTMS</span>':'')
      +(x.role?' <span class="pill am">'+x.role+'</span>':'')
      +'</td><td class="mono">'+esc(x.mac)+'</td><td>'+x.rssi+'</td><td class="mono">'+sv+'</td>'
      +'<td><button class="p" onclick="conn(\''+x.mac+'\')">Verbinden</button><br>'
      +'<button onclick="remember(\'bike\',\''+x.mac+'\',\''+jsq(x.name)+'\')">als Bike</button> '
      +'<button onclick="remember(\'hr\',\''+x.mac+'\',\''+jsq(x.name)+'\')">als Gurt</button></td></tr>';
  }).join('');
})}
function conn(mac){toast('scan-info','verbinde '+mac+' ...');
  post('/api/probe/connect',{mac:mac}).then(function(r){
    if(!r.ok){toast('scan-info',r.error||'connect fehlgeschlagen',1);return}
    curLink=r.link;toast('scan-info','Link '+r.link+' offen');
    renderGatt(r.gatt);
    document.querySelector('nav button[data-t="gatt"]').click();
  })}
function remember(role,mac,name){post('/api/probe/remember',{role:role,mac:mac,name:name})
  .then(function(){loadDevices()})}

// ── GATT ───────────────────────────────────────────────
function loadGatt(){if(curLink===null){return}
  get('/api/probe/gatt?link='+curLink).then(function(g){
    if(g.ok===false){toast('gatt-info',g.error||'kein Link',1);return}renderGatt(g)})}
function renderGatt(g){
  if(!g||!g.services){document.getElementById('gatt').innerHTML=
    '<span class="hint">Kein Dump. Erst verbinden.</span>';return}
  var s=g.summary||{};
  toast('gatt-info',esc(g.mac)+' · '+g.services.length+' Services · Befund: '+(s.verdict||'?'));
  document.getElementById('gatt').innerHTML=g.services.map(function(sv){
    var h='<div class="svc"><div>'+(sv.vendor?'<span class="vendor">':'<span>')+sv.uuid+'</span> '
      +esc(sv.label||(sv.vendor?'Vendor':''))+' <span class="hint">h '+sv.start+'-'+sv.end+'</span></div>'
      +'<table><tbody>';
    (sv.chars||[]).forEach(function(c){
      var p=c.props||{},f=[];
      if(p.read)f.push('R');if(p.write)f.push('W');if(p.writeNR)f.push('Wnr');
      if(p.notify)f.push('N');if(p.indicate)f.push('I');
      h+='<tr><td class="mono">'+c.uuid+'</td><td>'+esc(c.label||'')+'</td>'
       +'<td class="mono hint">'+f.join(' ')+' h'+c.handle+'</td><td style="text-align:right">'
       +(p.read?'<button onclick="rd(\''+c.uuid+'\')">Read</button> ':'')
       +(p.notify?'<button onclick="sub(\''+c.uuid+'\',\'notify\')">Notify</button> ':'')
       +(p.indicate?'<button onclick="sub(\''+c.uuid+'\',\'indicate\')">Indicate</button>':'')
       +'</td></tr><tr id="v-'+c.uuid+'" hidden><td colspan="4" class="mono"></td></tr>';
    });
    return h+'</tbody></table></div>';
  }).join('');
}
function rd(uuid){post('/api/probe/read',{link:curLink,uuid:uuid}).then(function(r){
  var row=document.getElementById('v-'+uuid);if(!row)return;row.hidden=false;
  row.firstChild.textContent=r.ok?(r.hex+'  ('+r.len+' Byte)'):('Fehler: '+(r.error||'?'));
  row.firstChild.style.color=r.ok?'var(--ok)':'var(--no)'})}
function sub(uuid,mode){post('/api/probe/subscribe',{link:curLink,uuid:uuid,mode:mode,enable:true})
  .then(function(r){cpOut((r.ok?'Abo ':'Abo fehlgeschlagen ')+uuid+' '+mode
    +(r.ok?'':' — '+(r.error||'')),!r.ok)})}

// ── Control Point ──────────────────────────────────────
function cp(hex){
  if(!hex)return;
  post('/api/probe/write',{link:curLink,uuid:'2AD9',hex:hex,awaitIndication:true,timeoutMs:3000})
  .then(function(r){
    if(!r.ok){cpOut('&gt; '+esc(hex)+'   ABGELEHNT: '+esc(r.error||'?'),1);return}
    var g=r.guard||{},resp=r.response||{},t='&gt; '+esc(r.hex)+'  '+esc(g.opcodeName||'');
    if(g.modified)t+='  [geklemmt: '+esc(g.reason)+']';
    if(resp.timeout)t+='\n  &lt; keine Antwort in '+resp.waitedMs+' ms';
    else t+='\n  &lt; '+esc(resp.hex)+'  '+esc(resp.resultName||resp.note||'')
      +(resp.success?'  OK':'  NICHT OK');
    cpOut(t,!resp.success)});
}
function cpOut(html,bad){var e=document.getElementById('cp-out');
  e.innerHTML='<div style="color:'+(bad?'var(--no)':'var(--tx)')+'">'+html+'</div>'+e.innerHTML;
  e.classList.remove('hint')}
function phase(p){if(!p)return;post('/api/probe/phase',{phase:p}).then(function(r){
  cpOut('Phase → '+esc(r.phase))})}

// ── Log ────────────────────────────────────────────────
function pollLog(){
  if(document.getElementById('t-log').hidden||!document.getElementById('tail').checked)return;
  fetch('/api/probe/log?since='+logSeq+'&max=120').then(function(r){return r.text()})
  .then(function(t){
    if(!t.trim())return;
    var box=document.getElementById('log'),out='';
    t.trim().split('\n').forEach(function(line){
      var e;try{e=JSON.parse(line)}catch(x){return}
      if(e.seq>logSeq)logSeq=e.seq;
      out+='<span class="d-'+e.dir+'">'+String(e.ts).padStart(9)+' '+e.dir.padEnd(8)
        +(e.link>=0?('L'+e.link):'  ')+' '+String(e.uuid||'').padEnd(6)+' '
        +esc(e.hex||e.msg||'')+' <span class="hint">'+esc(e.phase)+'</span></span>\n';
    });
    box.innerHTML+=out;box.scrollTop=box.scrollHeight;
    toast('log-info','bis seq '+logSeq);
  })}
setInterval(pollLog,1000);

// ── Keepalive ──────────────────────────────────────────
// Labor: Deadman nur nach Last-Opcodes. Keepalive alle 4 s, auch im
// Hintergrund-Tab (sonst trennt der Deadman trotz offenem Fenster).
setInterval(function(){
  var g=(S.probe||{}).guard||{};
  if(g.armed)post('/api/probe/keepalive');
},4000);

document.getElementById('stop').onclick=function(){
  post('/api/probe/panic',{reason:'not-stop per UI'}).then(function(){
    cpOut('NOT-STOP ausgeloest: 08 01 gesendet, Links getrennt',1)})};

// ── Config ─────────────────────────────────────────────
function loadCfg(){get('/api/config/get').then(function(c){
  document.getElementById('c-ctrl').checked=c.guardAllowControl;
  document.getElementById('c-sim').checked=c.guardAllowSim;
  document.getElementById('c-watt').value=c.guardMaxWatt;
  document.getElementById('c-level').value=c.guardMaxLevel;
  document.getElementById('c-dead').value=c.guardDeadmanS;
  document.getElementById('c-dmode').value=c.guardDeadmanMode||'lab';
  document.getElementById('c-arc').checked=!!c.autoReconnect;
  document.getElementById('c-swl').checked=!!c.scanWhileLinked;
  document.getElementById('c-host').value=c.hubHost;
  document.getElementById('c-port').value=c.hubPort;
  document.getElementById('c-name').value=c.deviceName;
  document.getElementById('c-bike').value=c.bikeMac||'';
  document.getElementById('c-biken').value=c.bikeName||'';
})}
function saveCfg(){post('/api/config/save',{
  guardAllowControl:document.getElementById('c-ctrl').checked,
  guardAllowSim:document.getElementById('c-sim').checked,
  guardMaxWatt:+document.getElementById('c-watt').value,
  guardMaxLevel:+document.getElementById('c-level').value,
  guardDeadmanS:+document.getElementById('c-dead').value,
  guardDeadmanMode:document.getElementById('c-dmode').value,
  autoReconnect:document.getElementById('c-arc').checked,
  scanWhileLinked:document.getElementById('c-swl').checked,
  hubHost:document.getElementById('c-host').value,
  hubPort:+document.getElementById('c-port').value,
  deviceName:document.getElementById('c-name').value,
  bikeMac:document.getElementById('c-bike').value,
  bikeName:document.getElementById('c-biken').value
}).then(function(r){toast('cfg-msg',r.ok?'gespeichert':'Fehler',!r.ok);loadCfg()})}

// ── OTA / Crash ────────────────────────────────────────
function ota(){var f=document.getElementById('fw').files[0];
  if(!f){toast('ota-msg','Datei waehlen',1);return}
  var fd=new FormData();fd.append('firmware',f);toast('ota-msg','laedt ...');
  fetch('/ota-upload',{method:'POST',body:fd}).then(function(r){return r.text()})
  .then(function(t){toast('ota-msg',t)})}
function crash(){
  if(!confirm('Reisst den Link ohne Stop-Kommando ab. Log vorher gesichert?'))return;
  post('/api/probe/crash',{confirm:'crash',mode:'restart'}).then(function(r){
    toast('ota-msg','Crash ausgeloest bei seq '+(r.lastSeq||0))})}

// ── SSE ────────────────────────────────────────────────
(function(){
  var es=new EventSource('/events');
  es.onmessage=function(m){try{renderStatus(JSON.parse(m.data))}catch(e){}};
  es.onerror=function(){setTimeout(function(){location.reload()},8000)};
})();
loadDevices();
</script></body></html>)HTML";
