#pragma once
#include <Arduino.h>

/*
  The control page, held in flash and served with send_P.

  Everything dynamic arrives as JSON from /info, /status and /panels, so this
  never has to be assembled in RAM -- building a 6 KB String per request on a
  device that is also receiving a DMX stream is a good way to fragment the heap.
*/
static const char PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>robo_face</title><style>
:root{--bg:#140d1c;--card:#1e1528;--line:#33264a;--ink:#f3e9d2;--dim:#a699b5;--hot:#ff9a3c}
*{box-sizing:border-box}
body{background:var(--bg);color:var(--ink);font:16px/1.5 system-ui,sans-serif;margin:0;
padding:16px;max-width:620px;margin-inline:auto}
h1{font-size:1.3rem;margin:0 0 2px;color:var(--hot)}
p.sub{margin:0 0 18px;color:var(--dim);font-size:.85rem}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;
padding:14px;margin-bottom:14px}
h2{font-size:.78rem;text-transform:uppercase;letter-spacing:.08em;margin:0 0 10px;color:var(--dim)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(70px,1fr));gap:8px}
.cliprow{display:flex;align-items:center;gap:10px;padding:5px 0;
border-bottom:1px solid #2a1f3d}
.cliprow:last-child{border-bottom:none}
.cliprow button{flex:1;text-align:left}
.cliprow label{margin:0;font-size:.78rem;color:var(--dim);white-space:nowrap;gap:5px}
.fgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(78px,1fr));gap:7px}
.ftile{background:#0b0710;border:1px solid var(--line);border-radius:7px;
padding:5px;text-align:center;cursor:pointer;position:relative}

.ftile canvas{width:100%;height:auto;image-rendering:pixelated;display:block}
.ftile span{font-size:.65rem;color:var(--dim);display:block;margin-top:3px}
.ftile.fanim{border-color:#5a4478}
.ftile.fanim span{color:#c0a8e0;font-weight:600}
/* Declared after .fanim: equal specificity, so the later rule must be the
   selected one or an animation tile never shows as picked. */
.ftile.on,.ftile.fanim.on{border-color:var(--hot);background:#241528}
.ftile.on span{color:var(--hot)}
/* A tick as well as colour, so selection does not rely on noticing a border */
.ftile.on::after{content:"\2713";position:absolute;top:2px;right:4px;
font-size:.7rem;color:var(--hot);font-weight:700;line-height:1}
.ftile.on span{color:var(--hot)}
button{background:var(--line);color:var(--ink);border:1px solid #4a3a68;border-radius:8px;
padding:11px 8px;font:inherit;font-size:.92rem;cursor:pointer}
button:active,button.on{background:var(--hot);color:var(--bg)}
canvas{width:100%;height:auto;image-rendering:pixelated;background:#0b0710;
border-radius:8px;border:1px solid var(--line)}
dl{display:grid;grid-template-columns:auto 1fr;gap:3px 12px;margin:0;font-size:.88rem}
dt{color:var(--dim)}dd{margin:0;font-variant-numeric:tabular-nums}
label{display:flex;align-items:center;gap:8px;font-size:.9rem;margin-bottom:8px}
input[type=number]{width:7em;background:#0b0710;color:var(--ink);border:1px solid var(--line);
border-radius:6px;padding:7px;font:inherit}
input[type=range]{flex:1;accent-color:var(--hot)}
.rots{display:flex;flex-wrap:wrap;gap:14px;margin:4px 0 12px}
.rots label{margin:0;font-size:.85rem}
.save{background:var(--hot);color:var(--bg);border-color:var(--hot);width:100%;margin-top:4px}
.ok{color:#7fd17f;font-size:.85rem;margin-left:8px}
</style></head><body>
<h1>robo_face</h1><p class=sub>ESP32-C3 &middot; E1.31 lip sync</p>

<section><h2>Live preview</h2><canvas id=cv width=200 height=90></canvas></section>

<section><h2>Face</h2><div class=grid>
<button onclick="g('/m?v=cycle')">Cycle</button>
<button onclick="g('/m?v=chomp')">Chomp</button>
<button onclick="g('/m?v=scream')">Scream</button>
<button onclick="g('/m?v=off')">Off</button>
</div></section>

<section><h2>Mode</h2><div class=grid id=modes></div>
<label style="margin-top:10px"><input type=checkbox id=calm onchange=saveCalm()>
calm idle &mdash; human expressions only, no chomping or screaming</label>
</section>

<section><h2>Speak</h2>
<div id=clips></div>
<div style="color:var(--dim);font-size:.78rem;margin:8px 0 4px">
Ticked clips are the ones motion may play. Tick none to allow all.
<a href=# onclick="allMotion(1);return false" style="color:var(--hot)">all</a> /
<a href=# onclick="allMotion(0);return false" style="color:var(--hot)">none</a></div>
<div class=grid style="margin-top:8px">
<button onclick="g('/play')">Random</button><button onclick="g('/stop')">Stop</button></div>
<label style="margin-top:12px">Volume <input type=range id=vol min=0 max=100
oninput="vl.textContent=this.value" onchange="g('/vol?v='+this.value)"><span id=vl>-</span></label>
</section>

<section><h2>Motion</h2>
<label><input type=checkbox id=men onchange=one(this)> trigger on motion</label>
<label>Sensor <select id=sens onchange=one(this)>
<option value=0>PIR (no range control)</option>
<option value=1>MaxSonar MB1242 (I2C)</option>
<option value=2>HC-SR04 (needs a divider on echo)</option>
</select></label>
<label>Speak within <input type=number id=trigcm min=20 max=600 onchange=one(this)> cm</label>
<label>PIR must hold <input type=number id=pirhold min=0 max=5000 step=100 onchange=one(this)> ms</label>
<label><input type=checkbox id=phys onchange=one(this)> physical toggle + button fitted</label>
<label><input type=checkbox id=toginv onchange=one(this)> toggle reads backwards</label>
<label>I2C speed <select id=i2ckhz onchange=one(this)>
<option value=100>100 kHz</option>
<option value=50>50 kHz</option>
<option value=20>20 kHz</option>
<option value=10>10 kHz</option>
</select></label>
<label>Cooldown <input type=number id=cd min=0 max=3600 onchange=one(this)> s</label>
<dl><dt>Type</dt><dd id=sensnm>-</dd>
<dt>Detecting</dt><dd id=mot>-</dd>
<dt>Distance</dt><dd id=dist>-</dd>
<dt>Cooldown left</dt><dd id=cdl>-</dd>
<dt>Triggers</dt><dd id=fire>-</dd>
<dt>Last clip</dt><dd id=lclip>-</dd>
<dt>Toggle</dt><dd id=tog>-</dd>
<dt>Button</dt><dd id=btn>-</dd></dl>
<div class=grid style="margin-top:8px"><button onclick=i2c()>Scan I2C</button>
<button onclick=i2cd()>Check bus</button></div>
<div id=i2cmsg style="color:var(--dim);font-size:.82rem;margin-top:6px"></div>
</section>

<section><h2>Phoneme test</h2><div class=grid id=ph></div></section>

<section><h2>Brightness</h2>
<label><input type=range id=br min=0 max=15 oninput="bl.textContent=this.value"
onchange="g('/b?v='+this.value)"><span id=bl>-</span></label></section>

<section><h2>WiFi</h2>
<dl><dt>State</dt><dd id=net2>-</dd><dt>Address</dt><dd id=ip2>-</dd></dl>
<div class=grid style="margin:10px 0"><button onclick=scan()>Scan</button></div>
<label>Network <select id=ap><option value="">-- scan first --</option></select></label>
<label>Password <input type=password id=pw placeholder="leave blank if open"></label>
<button class=save onclick=joinWifi()>Save &amp; restart</button>
<span id=wmsg class=ok></span></section>

<section><h2>Idle mouth frames</h2>
<div style="color:var(--dim);font-size:.78rem;margin-bottom:8px">
Tap a tile to play it on the prop. The first two are whole animations; the rest are still poses held for 10s each. Tick the ones to cycle through when idle,
for <b id=fmode>-</b>. Tick none to use the default set.
<a href=# onclick="allFrames(0);return false" style="color:var(--hot)">clear</a></div>
<div class=fgrid id=fgrid></div></section>

<section><h2>Settings</h2>
<label>WiFi <select id=wmode onchange=one(this)>
<option value=0>Auto &mdash; join a network, else host one</option>
<option value=1>Always host its own network</option>
<option value=2>Radio off</option>
</select></label>
<div style="color:var(--dim);font-size:.8rem;margin:-4px 0 10px">
Takes effect on reboot.</div>
<label>Matrix clock <select id=spi onchange=one(this)>
<option value=4000>4 MHz (default)</option>
<option value=2000>2 MHz</option>
<option value=1000>1 MHz</option>
<option value=500>500 kHz</option>
<option value=250>250 kHz</option>
</select></label>
<label>E1.31 universe <input type=number id=uni min=1 max=63999></label>
<label>Start channel <input type=number id=ch min=1 max=503></label>
<div><span style="color:var(--dim);font-size:.85rem">Rotate 180&deg;</span>
<div class=rots id=rots></div></div>
<button class=save onclick=saveAll()>Save</button><span id=saved class=ok></span>
</section>

<section><h2>Status</h2><dl>
<dt>Mode</dt><dd id=mdn>-</dd>
<dt>Network</dt><dd id=net>-</dd>
<dt>Resyncs</dt><dd id=rsy>-</dd>
<dt>Mouth</dt><dd id=src>-</dd>
<dt>Speaking</dt><dd id=spk>-</dd>
<dt>Phoneme</dt><dd id=pn>-</dd>
<dt>Channels</dt><dd id=chr>-</dd>
<dt>Packets</dt><dd id=pk>-</dd>
<dt>Discarded</dt><dd id=dc>-</dd>
<dt>Uptime</dt><dd id=up>-</dd>
<dt>Free heap</dt><dd id=hp>-</dd>
<dt>IP</dt><dd id=ip>-</dd>
</dl></section>

<script>
let MODS=4,NAMES=[],first=true,MODES=[],curMode=0;
const cv=document.getElementById('cv'),cx=cv.getContext('2d');
function g(u){return fetch(u)}
fetch('/info').then(r=>r.json()).then(j=>{
  MODS=j.mods;NAMES=j.ph;
  ph.innerHTML=NAMES.map((n,i)=>`<button onclick="g('/p?i=${i}')">${n}</button>`).join('');
  MODES=j.modes||[];
  modes.innerHTML=MODES.map((m,i)=>
    `<button id=md${i} onclick="setMode(${i})">${m.name}</button>`).join('');
  drawClips();
  loadFrames();
  rots.innerHTML=Array.from({length:MODS},(_,i)=>
    `<label><input type=checkbox id=r${i}>${i}</label>`).join('');
  layout(j.mouth);
});
let COLS=2,ROWS=2,MOUTH=2;
function layout(m){MOUTH=m;COLS=Math.max(2,m);cv.width=COLS*8*4;cv.height=2*8*4;}
function draw(hex){
  const px=4,W=COLS*8;
  cx.fillStyle='#0b0710';cx.fillRect(0,0,cv.width,cv.height);
  for(let d=0;d<MODS;d++){
    // eyes on the top row, mouth modules left-to-right beneath
    const ox=(d<2? d*8 + (W-16)/2 : (d-2)*8)*px, oy=(d<2?0:8)*px;
    for(let r=0;r<8;r++){
      const v=parseInt(hex.substr((d*8+r)*2,2),16);
      for(let b=0;b<8;b++){
        const on=(v>>(7-b))&1;
        cx.fillStyle=on?'#ff9a3c':'#1b1226';
        cx.fillRect(ox+b*px,oy+r*px,px-1,px-1);
      }
    }
  }
}
function clipKey(p){return p.replace(/^.*\//,'').replace(/\.pfc$/,'');}

let FRAMES=null;            // {count, mods, data} fetched once
function loadFrames(){
  fetch('/frames').then(r=>r.json()).then(j=>{FRAMES=j;drawFrames();});
}
function drawTile(cx,f){
  const bpf=8*FRAMES.mods;
  cx.fillStyle='#160f1f';cx.fillRect(0,0,FRAMES.mods*8,8);
  for(let r=0;r<8;r++){
    for(let mo=0;mo<FRAMES.mods;mo++){
      const off=(f*bpf+r*FRAMES.mods+mo)*2;
      const v=parseInt(FRAMES.data.substr(off,2),16);
      for(let b=0;b<8;b++){
        if((v>>(7-b))&1){cx.fillStyle='#ff9a3c';cx.fillRect(mo*8+b,r,1,1);}
      }
    }
  }
}
let animTimers=[];
function drawFrames(){
  if(!FRAMES||!MODES.length) return;
  animTimers.forEach(clearInterval); animTimers=[];
  const m=MODES[curMode];
  fmode.textContent=m.name;
  const sel=(m.iframes||'').split(',').filter(x=>x!=='').map(Number);
  fgrid.innerHTML='';

  // Animations first: they are whole behaviours, not poses, and showing
  // them moving is the only honest way to pick one.
  const items=(FRAMES.anims||[]).map(a=>({id:a.id,anim:a}))
    .concat(Array.from({length:FRAMES.count},(_,f)=>({id:f,anim:null})));

  for(const it of items){
    const on=sel.includes(it.id);
    const d=document.createElement('div');
    d.className='ftile'+(on?' on':'')+(it.anim?' fanim':'');
    d.innerHTML=`<canvas width="${FRAMES.mods*8}" height="8"></canvas>`
      +`<span>${it.anim?it.anim.name:it.id}</span>`;
    const cx=d.querySelector('canvas').getContext('2d');
    if(it.anim){
      let k=0;
      drawTile(cx,it.anim.frames[0]);
      animTimers.push(setInterval(()=>{
        k=(k+1)%it.anim.frames.length; drawTile(cx,it.anim.frames[k]);
      }, it.anim.ms*3));   // slowed for legibility at thumbnail size
    } else {
      drawTile(cx,it.id);
    }
    d.onclick=(e)=>{
      if(!e.shiftKey) g('/frame?i='+it.id);
      toggleFrame(it.id);
    };
    fgrid.appendChild(d);
  }
}
function toggleFrame(f){
  const m=MODES[curMode];
  let sel=(m.iframes||'').split(',').filter(x=>x!=='').map(Number);
  sel = sel.includes(f) ? sel.filter(x=>x!==f) : sel.concat([f]);
  m.iframes=sel.join(',');
  g(`/idleframes?mode=${curMode}&list=${encodeURIComponent(m.iframes)}`);
  drawFrames();
}
function allFrames(v){
  MODES[curMode].iframes='';
  g(`/idleframes?mode=${curMode}&list=`).then(drawFrames);
}
function drawClips(){
  const m=MODES[curMode];
  for(let i=0;i<MODES.length;i++){
    const b=document.getElementById('md'+i);
    if(b)b.className=(i==curMode)?'on':'';
  }
  if(!m||!m.clips.length){
    clips.innerHTML=`<span style="color:var(--dim);font-size:.85rem">no clips in ${
      m?m.name:'this mode'}/ &mdash; add them to data/${m?m.name:''}/ and run uploadfs</span>`;
    return;
  }
  // An empty stored list means every clip is allowed, so show them all ticked.
  const sel=(m.motion||'').split(',').filter(x=>x);
  const none=sel.length===0;
  calm.checked=!!m.calm;
  drawFrames();
  clips.innerHTML=m.clips.map(c=>{
    const k=clipKey(c);
    const on=none||sel.includes(k);
    return `<div class=cliprow><button onclick="g('/play?c=${c}')">${k}</button>`
      +`<label><input type=checkbox data-k="${k}" ${on?'checked':''}`
      +` onchange=saveMotion()>motion</label></div>`;
  }).join('');
}
function saveMotion(){
  const boxes=[...clips.querySelectorAll('input[type=checkbox]')];
  const on=boxes.filter(b=>b.checked).map(b=>b.dataset.k);
  // All ticked is stored as empty, i.e. "allow everything".
  const list=(on.length===boxes.length)?'':on.join(',');
  MODES[curMode].motion=list;
  g(`/mclips?mode=${curMode}&list=${encodeURIComponent(list)}`);
}
function allMotion(v){
  clips.querySelectorAll('input[type=checkbox]').forEach(b=>b.checked=!!v);
  saveMotion();
}
function setMode(i){curMode=i;g('/mode?v='+i).then(drawClips);}
function saveCalm(){
  MODES[curMode].calm=calm.checked?1:0;
  g(`/idle?mode=${curMode}&calm=${calm.checked?1:0}`);
}
function i2cd(){
  i2cmsg.textContent='checking...';
  fetch('/i2cdiag').then(r=>r.json()).then(j=>{
    const l=[];
    l.push(`SDA gpio${j.sda}: pull-up ${j.sda_pulled_up?'OK':'MISSING'}${j.sda_pin_ok?'':', pin held LOW'}`);
    l.push(`SCL gpio${j.scl}: pull-up ${j.scl_pulled_up?'OK':'MISSING'}${j.scl_pin_ok?'':', pin held LOW'}`);
    if(!j.sda_pulled_up||!j.scl_pulled_up) l.push('-> add 4.7k to 3V3, and check the sensor has power');
    else if(!j.sda_pin_ok||!j.scl_pin_ok) l.push('-> a line is shorted to ground');
    else l.push('-> bus idles high; try Scan I2C');
    i2cmsg.innerHTML=l.join('<br>');
  }).catch(()=>{i2cmsg.textContent='check failed';});
}
function i2c(){
  i2cmsg.textContent='scanning...';
  fetch('/i2c').then(r=>r.json()).then(j=>{
    if(j.found.length>8){
      i2cmsg.innerHTML='BUS FAULT - every address replied.<br>'
        +'A slave is holding SDA low, or the lines are shorted. '
        +'Press Check bus.';
    } else {
      i2cmsg.textContent = j.found.length
        ? `SDA ${j.sda} SCL ${j.scl} - replied at ${j.found.join(', ')}`
        : `SDA ${j.sda} SCL ${j.scl} - no reply at ${j.expect}. Check wiring and pull-ups.`;
    }
  }).catch(()=>{i2cmsg.textContent='scan failed';});
}
function scan(){
  wmsg.textContent='scanning...';
  fetch('/scan').then(r=>r.json()).then(l=>{
    l.sort((a,b)=>b.r-a.r);
    ap.innerHTML=l.length
      ? l.map(n=>`<option value="${n.s}">${n.s} (${n.r} dBm)${n.e?'':' open'}</option>`).join('')
      : '<option value="">none found</option>';
    wmsg.textContent=l.length+' found';
    setTimeout(()=>wmsg.textContent='',2500);
  }).catch(()=>{wmsg.textContent='scan failed';});
}
function joinWifi(){
  if(!ap.value){wmsg.textContent='pick a network';return;}
  wmsg.textContent='saving...';
  g(`/wifi?ssid=${encodeURIComponent(ap.value)}&pass=${encodeURIComponent(pw.value)}`)
    .then(()=>{wmsg.textContent='restarting - reconnect to your network';});
}
function tick(){
  fetch('/panels').then(r=>r.text()).then(draw).catch(()=>{});
}
function stat(){
  fetch('/status').then(r=>r.json()).then(j=>{
    if(j.mode!==curMode&&MODES.length){curMode=j.mode;drawClips();}
    src.textContent=j.src;pn.textContent=j.pn;
    spk.textContent=j.spk?'yes':'no';mdn.textContent=j.modenm;rsy.textContent=j.rsy;net.textContent=j.net;net2.textContent=j.net;ip2.textContent=j.ip;
    mot.textContent=j.warm?'warming up':(j.settle?'ignored (just spoke)':(j.mot?'yes':'no'));
    sensnm.textContent=j.sensnm;
    dist.textContent=(j.dist>=0)?(j.dist+' cm')
      :(j.sens==1&&!j.sok?'not responding':'n/a');
    cdl.textContent=j.cdl+' s';fire.textContent=j.fire;lclip.textContent=j.clip||'-';
    tog.textContent=j.phys?(j.tog?'motion on':'motion off'):'not fitted';
    btn.textContent=j.phys?(j.btn?'pressed':'up'):'not fitted';
    chr.textContent=j.ch0+'-'+j.ch1+' @ u'+j.uni;
    pk.textContent=j.pk;dc.textContent=j.dc;ip.textContent=j.ip;
    up.textContent=j.up+' s';hp.textContent=(j.hp/1024|0)+' KB';
    if(first){first=false;uni.value=j.uni;ch.value=j.ch0;br.value=j.br;bl.textContent=j.br;
      vol.value=j.vol;vl.textContent=j.vol;cd.value=j.cd;men.checked=!!j.men;
      spi.value=j.spi;wmode.value=j.wmode;sens.value=j.sens;trigcm.value=j.trigcm;
      phys.checked=!!j.phys;toginv.checked=!!j.toginv;i2ckhz.value=j.i2ckhz;pirhold.value=j.pirhold;
      for(let i=0;i<MODS;i++){const e=document.getElementById('r'+i);if(e)e.checked=!!(j.rot>>i&1);}}
  }).catch(()=>{});
}
// Map each input to its query parameter, so a control only ever writes
// its own setting. Sending the whole form let one unsynced field clobber
// unrelated settings.
const FIELD={uni:'uni',ch:'ch',cd:'cd',men:'mot',spi:'spi',wmode:'wmode',
             sens:'sensor',trigcm:'trigcm',i2ckhz:'i2ckhz',pirhold:'pirhold',
             phys:'phys',toginv:'toginv'};
function one(el){
  const k=FIELD[el.id]; if(!k) return;
  const v=(el.type=='checkbox')?(el.checked?1:0):el.value;
  g(`/save?${k}=${encodeURIComponent(v)}`).then(()=>{
    saved.textContent='saved';setTimeout(()=>saved.textContent='',1200);});
}
function saveAll(){
  let rot=0;
  for(let i=0;i<MODS;i++){const e=document.getElementById('r'+i);if(e&&e.checked)rot|=1<<i;}
  g(`/save?rot=${rot}`).then(()=>{
    saved.textContent='saved';setTimeout(()=>saved.textContent='',1500);});
}
stat();setInterval(stat,1000);setInterval(tick,125);tick();
</script></body></html>)HTML";
