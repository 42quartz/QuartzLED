#pragma once
#include <Arduino.h>

// Single-page control UI served at http://quartzled.local/ — talks to /api with the v1 schema.
// Sliders use the same low-end-dense curve as HomeKit (state.h sliderToValue): 50% -> 10%.
static const char kWebUi[] PROGMEM = R"HTML(<!doctype html>
<html lang="tr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#111"><meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes"><title>QuartzLED</title>
<style>
:root{color-scheme:dark;--bg:#111;--card:#1c1c1e;--fg:#f2f2f7;--mute:#8e8e93;--acc:#ffb340;--line:#333}
*{box-sizing:border-box}body{margin:0;font:16px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg);
padding:env(safe-area-inset-top) 14px 28px}main{max-width:560px;margin:auto}
h1{font-size:20px;font-weight:600;margin:18px 0 10px;display:flex;justify-content:space-between;align-items:center}
h2{font-size:13px;font-weight:600;color:var(--mute);text-transform:uppercase;letter-spacing:.04em;margin:18px 4px 6px}
.card{background:var(--card);border-radius:14px;padding:12px 14px;margin:8px 0}
label{display:flex;justify-content:space-between;color:var(--mute);font-size:14px;margin-bottom:6px}
input[type=range]{width:100%;accent-color:var(--acc);height:28px;margin:0}
.colors{display:flex;gap:10px}.colors>div{flex:1}
input[type=color]{width:100%;height:46px;border:0;border-radius:10px;background:none;padding:0}
select{width:100%;padding:10px;border-radius:10px;background:#2c2c2e;color:var(--fg);border:1px solid var(--line);font-size:15px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(100px,1fr));gap:7px}
.grid button,.row button{padding:10px 4px;border-radius:10px;border:1px solid var(--line);background:#2c2c2e;color:var(--fg);font-size:14px;position:relative}
.grid button.on{background:var(--acc);color:#111;border-color:var(--acc);font-weight:600}
.grid button .x{position:absolute;top:-6px;right:-6px;width:20px;height:20px;border-radius:50%;background:#555;color:#fff;font-size:12px;line-height:20px}
.row{display:flex;gap:7px;flex-wrap:wrap}.row button{flex:1;min-width:64px}
.tg{display:flex;justify-content:space-between;align-items:center;padding:6px 0}
.sw{width:52px;height:30px;border-radius:15px;background:#3a3a3c;border:0;position:relative;flex:none}
.sw::after{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;border-radius:50%;background:#fff;transition:.2s}
.sw.on{background:#34c759}.sw.on::after{left:25px}
.note{color:var(--mute);font-size:13px;margin:4px 0 10px}
.seg{display:flex;background:#2c2c2e;border-radius:10px;padding:3px;margin-bottom:10px}
.seg button{flex:1;padding:9px;border:0;border-radius:8px;background:none;color:var(--fg);font-size:14px}
.seg button.on{background:var(--acc);color:#111;font-weight:600}
.tl{display:flex;gap:8px;align-items:center;margin:8px 0}.tl span{width:64px;color:var(--mute);font-size:14px}
.tl input,.tl select{flex:1;min-width:0;padding:9px;border-radius:10px;background:#2c2c2e;color:var(--fg);border:1px solid var(--line);font-size:15px}
.tl button{padding:9px 12px;border-radius:10px;border:1px solid var(--line);background:#2c2c2e;color:var(--fg)}
.hide{display:none}#st{color:var(--mute);font-size:12px;text-align:center;margin-top:14px}
</style></head><body><main>
<h1>QuartzLED <button id="pw" class="sw" aria-label="Aç/Kapa"></button></h1>
<div class="card"><label>Parlaklık <span id="briv"></span></label><input id="bri" type="range" min="0" max="1000"></div>

<h2>Sahneler</h2><div class="card"><div class="grid" id="pre"></div>
<div class="row" style="margin-top:8px"><button id="savep">+ Şu anki ayarı kaydet</button></div></div>

<h2>Efekt</h2><div class="card"><div class="grid" id="fx"></div></div>

<h2>Ayarlar</h2>
<div class="card colors" id="colc"><div id="c1w"><label>Renk</label><input id="col" type="color"></div>
<div id="c2w"><label>İkinci renk</label><input id="col2" type="color"></div></div>
<div class="card" id="palc"><label>Palet</label><select id="pal"></select></div>
<div class="card"><label>Hız <span id="spv"></span></label><input id="sp" type="range" min="0" max="1000"></div>
<div class="card" id="inc"><label><span id="inl">Yoğunluk</span><span id="inv"></span></label><input id="in" type="range" min="0" max="255"></div>
<div class="card"><div class="tg">Ters yön <button id="rev" class="sw"></button></div>
<div class="tg">Ortadan aynala <button id="mir" class="sw"></button></div></div>

<h2>Zamanlayıcı</h2><div class="card">
<label>Kapanma <span id="tmv"></span></label>
<div class="row" id="tm"></div>
</div>

<h2>Ayılma</h2><div class="card">
<div class="tg">Otomatik kurulum <button id="arm" class="sw"></button></div>
<p class="note">Açıkken ışık kapalı olsa bile her gün gün doğumunda aydınlanır, gün batımında kararıp kapanır.</p>
<div class="seg" id="var"><button data-v="interval">Aralıklı</button><button data-v="auto">Otomatik</button></div>
<div id="vi">
 <div class="tl"><span>Uyanış</span><input id="wk" type="time"><select id="wkd"></select></div>
 <div class="tl"><span>Uyku</span><input id="sl" type="time"><select id="sld"></select></div>
 <div class="tg">Uyanış öncesi (uyarlanır) <button id="pre" class="sw"></button></div>
 <p class="note">Açıkken uyanıştan önceki ışık süresini MiniBeyaz, sabah anketindeki uyanma saatinize ve yorgunluğunuza göre ayarlar.</p>
</div>
<div id="va">
 <div class="row"><button id="geo">📍 Konumumu kullan</button></div>
 <div class="tl"><input id="lat" placeholder="Enlem" inputmode="decimal"><input id="lon" placeholder="Boylam" inputmode="decimal"><button id="lls">Kaydet</button></div>
</div>
<p class="note" id="cinfo"></p>
<label style="margin-top:10px">Hemen başlat: gün doğumu</label><div class="row" id="sr"></div>
<label style="margin-top:10px">Hemen başlat: gün batımı</label><div class="row" id="ss"></div></div>
<p id="st"></p>
</main><script>
const FX={solid:"Sabit",rainbow:"Gökkuşağı",colorloop:"Renk Döngüsü",breathe:"Nefes",chase:"Kayan Işık",
 scanner:"Tarayıcı",meteor:"Meteor",theater:"Tiyatro",twocolor:"İki Renk",gradient:"Gradyan",wave:"Dalga",
 noise:"Akış",confetti:"Konfeti",juggle:"Hokkabaz",fire:"Ateş",candle:"Mum",twinkle:"Pırıltı",sparkle:"Işıltı",
 pulse:"Nabız",heartbeat:"Kalp Atışı",police:"Polis",sunrise:"Gün Doğumu",sunset:"Gün Batımı"};
const PAL={rainbow:"Gökkuşağı",party:"Parti",ocean:"Okyanus",lava:"Lav",forest:"Orman",heat:"Isı",cloud:"Bulut",
 sunset:"Gün Batımı",aurora:"Kuzey Işıkları",pastel:"Pastel",colors:"Renklerim"};
const PRE={okuma:"Okuma",odak:"Odak",film:"Film",gece:"Gece Lambası",rahat:"Rahatlama",kutup:"Kuzey Işıkları",
 gunbatimi:"Alacakaranlık",somine:"Şömine",mum:"Mum Işığı",romantik:"Romantik",parti:"Parti",disko:"Disko"};
const INT={rainbow:"Tekrar",colorloop:"Pastellik",chase:"Kuyruk",scanner:"Genişlik",meteor:"Boyut",twocolor:"Blok",
 wave:"Tekrar",noise:"Doku",confetti:"Yoğunluk",juggle:"Nokta",fire:"Kıvılcım",candle:"Titreme",twinkle:"Yoğunluk",
 sparkle:"Işıltı",pulse:"Halka"};
const C1=new Set(["solid","breathe","chase","scanner","meteor","theater","twocolor","gradient","candle","twinkle",
 "sparkle","pulse","heartbeat"]),C2=new Set(["theater","twocolor","gradient"]),PF=new Set(["wave","noise","confetti","juggle"]);
const $=id=>document.getElementById(id);let S={},C={},busy=0,timer;
const toVal=p=>(Math.pow(81,p)-1)/80,toPos=v=>Math.log(1+80*v)/Math.log(81);
const hex=c=>"#"+c.map(v=>v.toString(16).padStart(2,"0")).join(""),rgb=h=>[1,3,5].map(i=>parseInt(h.substr(i,2),16));
const pct=v=>Math.round(v/10)+"%",mmss=s=>Math.floor(s/60)+" dk "+(s%60)+" sn";
function vis(){const e=S.effect,pc=PF.has(e);$("c1w").classList.toggle("hide",!C1.has(e));
 $("c2w").classList.toggle("hide",!(C2.has(e)||(pc&&S.palette=="colors")));
 $("colc").classList.toggle("hide",!C1.has(e)&&!C2.has(e)&&!(pc&&S.palette=="colors"));
 $("palc").classList.toggle("hide",!pc);$("inc").classList.toggle("hide",!INT[e]);$("inl").textContent=INT[e]||""}
function render(s){S=s;$("pw").classList.toggle("on",s.on);$("rev").classList.toggle("on",s.reverse);
 $("mir").classList.toggle("on",s.mirror);
 if(!busy){$("bri").value=Math.round(toPos(s.bri/255)*1000);$("sp").value=Math.round(toPos(s.speed/1000)*1000);
  $("col").value=hex(s.color);$("col2").value=hex(s.color2);$("pal").value=s.palette;$("in").value=s.intensity}
 $("briv").textContent=pct($("bri").value);$("spv").textContent=s.speed;$("inv").textContent=Math.round(s.intensity/2.55)+"%";
 $("tmv").textContent=s.timer_s?mmss(s.timer_s)+" sonra":"kapalı";
 for(const b of $("fx").children)b.classList.toggle("on",b.dataset.k==s.effect);vis()}
async function api(body,cmd="set"){const r=await fetch("/api",body?{method:"POST",headers:{"Content-Type":"application/json"},
 body:JSON.stringify(Object.assign({v:1,cmd,source:"web"},body))}:{});const j=await r.json();
 if(j.state)render(j.state);if(j.presets)presets(j.presets);$("st").textContent=j.error?("Hata: "+j.error):"";return j}
function crender(c){C=c;$("arm").classList.toggle("on",c.armed);
 for(const b of $("var").children)b.classList.toggle("on",b.dataset.v==c.variant);
 $("vi").classList.toggle("hide",c.variant!="interval");$("va").classList.toggle("hide",c.variant!="auto");
 if(document.activeElement.tagName!="INPUT"){$("wk").value=c.wake;$("sl").value=c.sleep;
  if(c.located){$("lat").value=c.lat;$("lon").value=c.lon}}
 $("wkd").value=c.wake_dur;$("pre").classList.toggle("on",!!c.prewake);$("wkd").disabled=!!c.prewake;$("sld").value=c.sleep_dur;const t=c.today||{};
 $("cinfo").textContent=!c.synced?"Saat henüz senkron değil…":(c.variant=="auto"&&!c.located)?"Konum gerekli.":
  "Bugün: aydınlanma "+(t.rise_start||"–")+" → "+(t.rise_end||"–")+" · kararma "+(t.set_start||"–")+" → "+(t.set_end||"–")+
  " · saat "+c.now+(c.armed?"":" · otomatik kurulum kapalı")}
async function capi(body){const j=await api(body,"circadian");if(j.circadian)crender(j.circadian)}
// Leaving a running sunrise/sunset without the plan armed: offer to arm it.
async function guard(){if(!["sunrise","sunset"].includes(S.effect)||C.armed)return;
 if(confirm("Ayılma otomatik kurulumu kapalı. Mod değişince bu geçiş biter ve ışık yarın kendiliğinden açılmaz.\n\nOtomatik kurulumu açayım mı?"))await capi({armed:true})}
function send(body,delay=120){busy=1;clearTimeout(timer);timer=setTimeout(()=>api(body).finally(()=>busy=0),delay)}
function btn(parent,label,key,fn){const b=document.createElement("button");b.textContent=label;b.dataset.k=key;
 b.onclick=fn;parent.appendChild(b);return b}
function presets(p){const g=$("pre");g.innerHTML="";for(const n of p.builtin)btn(g,PRE[n]||n,n,async()=>{await guard();api({name:n},"preset")});
 for(const n of p.user){const b=btn(g,"★ "+n,n,()=>api({name:n},"preset"));const x=document.createElement("span");
  x.className="x";x.textContent="×";x.onclick=e=>{e.stopPropagation();if(confirm(n+" silinsin mi?"))api({delete:n},"preset")};
  b.appendChild(x)}}
for(const k in FX)btn($("fx"),FX[k],k,async()=>{if(k!=S.effect)await guard();api({effect:k,on:true})});
for(const k in PAL){const o=document.createElement("option");o.value=k;o.textContent=PAL[k];$("pal").appendChild(o)}
for(const m of[15,30,60,120])btn($("tm"),m<60?m+" dk":m/60+" sa","",()=>api({minutes:m},"timer"));
btn($("tm"),"İptal","",()=>api({minutes:0},"timer"));
for(const m of[10,20,30]){btn($("sr"),m+" dk","",()=>api({minutes:m},"sunrise"));btn($("ss"),m+" dk","",()=>api({minutes:m},"sunset"))}
for(const id of["wkd","sld"])for(const m of[10,15,20,30,45,60]){const o=document.createElement("option");o.value=m;o.textContent=m+" dk";$(id).appendChild(o)}
$("arm").onclick=()=>capi({armed:!C.armed});$("pre").onclick=()=>capi({prewake:!C.prewake});
for(const b of $("var").children)b.onclick=()=>capi({variant:b.dataset.v});
$("wk").onchange=e=>capi({wake:e.target.value});$("sl").onchange=e=>capi({sleep:e.target.value});
$("wkd").onchange=e=>capi({wake_dur:+e.target.value});$("sld").onchange=e=>capi({sleep_dur:+e.target.value});
$("lls").onclick=()=>{const la=parseFloat($("lat").value.replace(",",".")),lo=parseFloat($("lon").value.replace(",","."));
 if(isNaN(la)||isNaN(lo))return alert("Enlem ve boylamı sayı olarak girin (ör. 41.01 ve 28.97).");capi({lat:la,lon:lo})};
$("geo").onclick=()=>{if(!window.isSecureContext||!navigator.geolocation)
  return alert("Tarayıcı konumu yalnızca HTTPS üzerinden verir. Tailscale adresinden (:8443) açın ya da enlem/boylamı elle girin.");
 navigator.geolocation.getCurrentPosition(p=>capi({lat:+p.coords.latitude.toFixed(4),lon:+p.coords.longitude.toFixed(4),variant:"auto"}),
  e=>alert("Konum alınamadı: "+e.message),{timeout:15000})};
$("pw").onclick=()=>api({on:!S.on});$("rev").onclick=()=>api({reverse:!S.reverse});$("mir").onclick=()=>api({mirror:!S.mirror});
$("bri").oninput=e=>{$("briv").textContent=pct(e.target.value);send({bri:Math.max(1,Math.round(toVal(e.target.value/1000)*255)),on:true})};
$("sp").oninput=e=>{const v=Math.round(toVal(e.target.value/1000)*1000);$("spv").textContent=v;send({speed:v})};
$("in").oninput=e=>{$("inv").textContent=Math.round(e.target.value/2.55)+"%";send({intensity:+e.target.value})};
$("col").oninput=e=>send({color:rgb(e.target.value),on:true});$("col2").oninput=e=>send({color2:rgb(e.target.value)});
$("pal").onchange=e=>api({palette:e.target.value});
$("savep").onclick=()=>{const n=prompt("Sahne adı:");if(n)api({save:n.trim()},"preset")};
const poll=()=>document.hidden||busy||api().catch(()=>$("st").textContent="Bağlantı yok");
api().catch(()=>$("st").textContent="Bağlantı yok");api({},"preset");capi({});setInterval(poll,4000);setInterval(()=>document.hidden||capi({}),30000);
document.addEventListener("visibilitychange",poll);
</script></body></html>)HTML";
