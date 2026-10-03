#pragma once
#include <Arduino.h>

// Single-page control UI served at http://led.local/ — talks to /api with the v1 schema.
static const char kWebUi[] PROGMEM = R"HTML(<!doctype html>
<html lang="tr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#111"><meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes"><title>MiniBeyaz LED</title>
<style>
:root{color-scheme:dark;--bg:#111;--card:#1c1c1e;--fg:#f2f2f7;--mute:#8e8e93;--acc:#ffb340}
*{box-sizing:border-box}body{margin:0;font:16px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg);
padding:env(safe-area-inset-top) 16px 24px}main{max-width:480px;margin:auto}
h1{font-size:20px;font-weight:600;margin:20px 0 12px;display:flex;justify-content:space-between;align-items:center}
.card{background:var(--card);border-radius:14px;padding:14px 16px;margin:12px 0}
label{display:flex;justify-content:space-between;color:var(--mute);font-size:14px;margin-bottom:8px}
input[type=range]{width:100%;accent-color:var(--acc);height:28px}
input[type=color]{width:100%;height:52px;border:0;border-radius:10px;background:none;padding:0}
.fx{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.fx button{padding:12px 4px;border-radius:10px;border:1px solid #333;background:#2c2c2e;color:var(--fg);font-size:14px}
.fx button.on{background:var(--acc);color:#111;border-color:var(--acc);font-weight:600}
.sw{width:52px;height:30px;border-radius:15px;background:#3a3a3c;border:0;position:relative}
.sw::after{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;border-radius:50%;background:#fff;transition:.2s}
.sw.on{background:#34c759}.sw.on::after{left:25px}
#st{color:var(--mute);font-size:12px;text-align:center}
</style></head><body><main>
<h1>MiniBeyaz LED <button id="pw" class="sw" aria-label="Aç/Kapa"></button></h1>
<div class="card"><label>Parlaklık <span id="briv"></span></label><input id="bri" type="range" min="1" max="255"></div>
<div class="card"><label>Renk</label><input id="col" type="color"></div>
<div class="card"><label>Efekt</label><div class="fx" id="fx"></div></div>
<div class="card"><label>Hız <span id="spv"></span></label><input id="sp" type="range" min="0" max="1000" step="10"></div>
<p id="st"></p>
</main><script>
const N={solid:"Sabit",rainbow:"Gökkuşağı",chase:"Kayan",breathe:"Nefes",fire:"Ateş",twinkle:"Pırıltı"};
const $=id=>document.getElementById(id);let S={},busy=0,timer;
const hex=c=>"#"+c.map(v=>v.toString(16).padStart(2,"0")).join("");
const rgb=h=>[1,3,5].map(i=>parseInt(h.substr(i,2),16));
function render(s){S=s;$("pw").classList.toggle("on",s.on);
 if(!busy){$("bri").value=s.bri;$("sp").value=s.speed;$("col").value=hex(s.color)}
 $("briv").textContent=Math.round(s.bri/2.55)+"%";$("spv").textContent=s.speed;
 for(const b of $("fx").children)b.classList.toggle("on",b.dataset.fx==s.effect)}
async function api(body){const r=await fetch("/api",body?{method:"POST",headers:{"Content-Type":"application/json"},
 body:JSON.stringify(Object.assign({v:1,cmd:"set",source:"web"},body))}:{});const j=await r.json();
 if(j.state)render(j.state);$("st").textContent=j.error?("Hata: "+j.error):"";return j}
function send(body,delay=120){busy=1;clearTimeout(timer);timer=setTimeout(()=>api(body).finally(()=>busy=0),delay)}
for(const k in N){const b=document.createElement("button");b.textContent=N[k];b.dataset.fx=k;
 b.onclick=()=>api({effect:k,on:true});$("fx").appendChild(b)}
$("pw").onclick=()=>api({on:!S.on});
$("bri").oninput=e=>{$("briv").textContent=Math.round(e.target.value/2.55)+"%";send({bri:+e.target.value,on:true})};
$("sp").oninput=e=>{$("spv").textContent=e.target.value;send({speed:+e.target.value})};
$("col").oninput=e=>send({color:rgb(e.target.value),on:true});
const poll=()=>document.hidden||busy||api().catch(()=>$("st").textContent="Bağlantı yok");
api().catch(()=>$("st").textContent="Bağlantı yok");setInterval(poll,4000);document.addEventListener("visibilitychange",poll);
</script></body></html>)HTML";
