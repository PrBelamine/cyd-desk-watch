#include "TaskWeb.h"
#include "Settings.h"
#include "TimeZones.h"
#include "DeskLogic.h"
#include "Radio.h"
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {
WebServer server(80);
TaskStore* tasks = nullptr;
void (*changed)() = nullptr;
bool started = false;
Config* config = nullptr;
void (*settingsChanged)() = nullptr;
void (*radiosChanged)() = nullptr;

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Digital Post-it</title>
<style>
:root{--note:#ffe066;--ink:#3b2f0b;--rule:#e6c84a;--ok:#2e8b57;--bad:#b3261e}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;background:#1c1b18;color:var(--ink);font:17px/1.4 system-ui,sans-serif;display:flex;justify-content:center;padding:16px}
main{width:100%;max-width:480px;background:var(--note);border-radius:6px;padding:20px 18px 24px;box-shadow:0 10px 30px #0008;align-self:flex-start}
h1{margin:0 0 4px;font-size:1.4rem}
#count{margin:0 0 14px;opacity:.7;font-size:.9rem}
form{display:flex;gap:8px;margin-bottom:12px}
input[type=text]{flex:1;min-width:0;font:inherit;padding:10px;border:2px solid var(--ink);border-radius:6px;background:#fffbe6;color:var(--ink)}
button{font:inherit;font-weight:600;padding:10px 14px;border:2px solid var(--ink);border-radius:6px;background:var(--ink);color:var(--note);cursor:pointer}
button.ghost{background:transparent;color:var(--ink)}
ul{list-style:none;margin:0;padding:0}
li{display:flex;align-items:center;gap:12px;padding:10px 0;border-bottom:1px solid var(--rule)}
li input{width:26px;height:26px;accent-color:var(--ok);flex:none}
li span{flex:1;word-break:break-word}
li.done span{text-decoration:line-through;opacity:.55}
li button{padding:2px 10px;font-size:1.2rem;line-height:1.2}
#msg{min-height:1.4em;margin:10px 0 0;color:var(--bad);font-size:.9rem}
#empty{opacity:.65;padding:14px 0}
footer{display:flex;justify-content:space-between;align-items:center;margin-top:10px;gap:8px;flex-wrap:wrap}
footer a{color:var(--ink);font-weight:600}
button,input[type=text]{min-height:44px}
@media(max-width:400px){body{padding:10px}main{padding:16px 14px 20px}form{flex-wrap:wrap}form button{flex:1 1 100%}}
@media(min-width:760px){body{padding:32px 16px}main{max-width:680px;padding:28px 32px 34px}h1{font-size:1.7rem}}
</style></head><body><main>
<h1 id="h1"></h1>
<p id="count"></p>
<form id="f"><input id="t" type="text" maxlength="60" autocomplete="off"><button id="add"></button></form>
<ul id="l"></ul><p id="empty" hidden></p>
<footer><button class="ghost" id="clr" type="button"></button></footer>
<p id="msg" role="status"></p></main>
<script>
const $=s=>document.querySelector(s);
const TX={en:{title:'Digital Post-it',ph:'New task',add:'Add',clr:'Delete finished tasks',set:'Location',load:'Loading…',empty:'Nothing to do right now.',none:'Empty list',done:'Done',del:'Delete',
 cnt:(k,n,f)=>k+' of '+n+' done · '+f+' free slot'+(f===1?'':'s'),full:'List full: 18 tasks maximum.',bad:'Empty or invalid text.',off:'The Post-it does not answer. Check the Wi-Fi.'},
 fr:{title:'Post-it numérique',ph:'Nouvelle tâche',add:'Ajouter',clr:'Supprimer les tâches terminées',set:'Lieu',load:'Chargement…',empty:'Rien à faire pour le moment.',none:'Liste vide',done:'Terminée',del:'Supprimer',
 cnt:(k,n,f)=>k+' sur '+n+' faites · '+f+' place'+(f===1?'':'s')+' libre'+(f===1?'':'s'),full:'Liste pleine : 18 tâches maximum.',bad:'Texte vide ou invalide.',off:'Le Post-it ne répond pas. Vérifiez le Wi-Fi.'}};
let lang=(navigator.language||'en').toLowerCase().startsWith('fr')?'fr':'en',last=null;
const T=()=>TX[lang];
function texts(){document.documentElement.lang=lang;document.title=T().title;$('#h1').textContent=T().title;$('#t').placeholder=T().ph;$('#t').setAttribute('aria-label',T().ph);
  $('#add').textContent=T().add;$('#clr').textContent=T().clr;$('#empty').textContent=T().empty;if(!last)$('#count').textContent=T().load}
function msg(m){$('#msg').textContent=m}
function render(d){
  last=d;const l=$('#l');l.textContent='';
  for(const t of d.tasks){
    const li=document.createElement('li');if(t.done)li.className='done';
    const c=document.createElement('input');c.type='checkbox';c.checked=t.done;c.setAttribute('aria-label',T().done);
    c.onchange=()=>call('/api/toggle','id='+t.id);
    const s=document.createElement('span');s.textContent=t.text;
    const x=document.createElement('button');x.className='ghost';x.textContent='×';x.title=T().del;x.setAttribute('aria-label',T().del);
    x.onclick=()=>call('/api/delete','id='+t.id);
    li.append(c,s,x);l.append(li);
  }
  const n=d.tasks.length,k=d.tasks.filter(t=>t.done).length;
  $('#count').textContent=n?T().cnt(k,n,d.max-n):T().none;
  $('#empty').hidden=n>0;
}
async function call(path,body){
  try{
    const r=await fetch(path,{method:body===undefined?'GET':'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const d=await r.json();
    if(!r.ok){msg(d.error==='full'?T().full:T().bad);return false}
    render(d);msg('');return true;
  }catch(e){msg(T().off);return false}
}
$('#f').onsubmit=async e=>{e.preventDefault();const v=$('#t').value.trim();if(!v)return;if(await call('/api/add','text='+encodeURIComponent(v)))$('#t').value=''};
$('#clr').onclick=()=>call('/api/clear','');
texts();
fetch('/api/settings').then(r=>r.json()).then(s=>{lang=s.lang==='fr'?'fr':'en';texts();if(last)render(last)}).catch(()=>{});
call('/api/tasks');
setInterval(()=>{if(!document.hidden)call('/api/tasks')},4000);
</script></body></html>)HTML";

const char SETTINGS_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Desk Watch</title>
<style>
:root{--bg:#10151d;--card:#18202b;--ink:#e8edf5;--muted:#9aa6b8;--line:#2a3544;--acc:#4da3ff;--ok:#7ee0a0;--bad:#ff8a80}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:17px/1.45 system-ui,sans-serif;padding:16px;display:flex;justify-content:center}
main{width:100%;max-width:600px}
h1{margin:4px 0 14px;font-size:1.5rem}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px 16px;margin-bottom:14px}
.now{display:grid;grid-template-columns:auto 1fr;gap:4px 14px}
.now span{color:var(--muted)} .now b{font-weight:600;word-break:break-word}
.row{display:flex;gap:8px}
input,select,button{font:inherit;min-height:48px;border-radius:10px;border:1px solid var(--line);background:#0e131a;color:var(--ink);padding:10px 12px}
input{flex:1;min-width:0}
select{width:100%}
button{cursor:pointer;background:var(--acc);color:#04121f;font-weight:700;border:0;padding:10px 18px}
button.ghost{background:transparent;color:var(--acc);border:1px solid var(--acc);width:100%;margin-top:12px}
ul{list-style:none;margin:10px 0 0;padding:0}
li button{display:block;width:100%;text-align:left;background:#0e131a;color:var(--ink);font-weight:500;border:1px solid var(--line);margin-top:8px}
li button small{display:block;color:var(--muted);font-weight:400}
li button:active,li button:hover{border-color:var(--acc)}
#msg{min-height:1.5em;margin:4px 2px 12px;color:var(--ok)} #msg.no{color:var(--bad)}
details{padding:0} summary{cursor:pointer;padding:14px 16px;font-weight:600;min-height:48px}
details>div{padding:0 16px 16px} label{display:block;margin:12px 0 4px;color:var(--muted);font-size:.9rem}
.seg{display:flex;gap:8px}.seg button{flex:1;background:#0e131a;color:var(--ink);border:1px solid var(--line)}.seg button.on{background:var(--acc);color:#04121f;border-color:var(--acc)}
@media(max-width:400px){body{padding:10px}.card{padding:12px}.row{flex-wrap:wrap}.row button{flex:1 1 100%}}
@media(min-width:760px){body{padding:36px 16px}main{max-width:680px}h1{font-size:1.8rem}}
</style></head><body><main>
<h1 id="h1"></h1>
<div class="card now"><span id="kW"></span><b id="vW">-</b><span id="kT"></span><b id="vT">-</b></div>
<form id="f" class="row"><input id="q" type="search" maxlength="60" autocomplete="off" enterkeyhint="search"><button type="submit" id="go"></button></form>
<ul id="res"></ul>
<p id="msg" role="status"></p>
<div class="card"><div id="lT" style="color:var(--muted);margin-bottom:8px"></div><div class="seg"><button type="button" data-l="fr">Français</button><button type="button" data-l="en">English</button></div></div>
<details class="card"><summary id="aT"></summary><div>
<label id="cL" for="cc"></label><select id="cc"></select>
<label id="tL" for="city"></label><select id="city"></select>
<button class="ghost" id="auto" type="button"></button>
</div></details>
</main>
<script>
const $=s=>document.querySelector(s);
const TX={en:{h1:'Location',kW:'Weather',kT:'Clock',lT:'Language of the watch',ph:'Search your town (e.g. Hue)',go:'Search',none:'No result. Try another spelling.',
  aT:'Different time zone (advanced)',cL:'Country',tL:'City',auto:'Use the time zone of this device',off:'The search needs Internet on this phone or computer.',err:'Could not save. Check the Wi-Fi.',
  both:t=>'Saved: weather and clock set for '+t,wx:t=>'Saved: weather set for '+t+'. Choose its time zone in the advanced section.',tz:t=>'Time zone saved: '+t,lang:'Language saved.'},
 fr:{h1:'Lieu',kW:'Météo',kT:'Heure',lT:'Langue de la montre',ph:'Chercher votre ville (ex. Hué)',go:'Chercher',none:'Aucun résultat. Essayez une autre orthographe.',
  aT:'Fuseau horaire différent (avancé)',cL:'Pays',tL:'Ville',auto:'Utiliser le fuseau de cet appareil',off:'La recherche demande Internet sur ce téléphone ou cet ordinateur.',err:'Enregistrement impossible. Vérifiez le Wi-Fi.',
  both:t=>'Enregistré : météo et heure réglées pour '+t,wx:t=>'Enregistré : météo réglée pour '+t+'. Choisissez son fuseau dans la partie avancée.',tz:t=>'Fuseau enregistré : '+t,lang:'Langue enregistrée.'}};
let S={lang:(navigator.language||'en').toLowerCase().startsWith('fr')?'fr':'en',tz:'',label:'',place:''},D=null,R=[],timer=0;
const L=()=>TX[S.lang]||TX.en;
const cname=c=>S.lang==='fr'?c[2]:c[1];
const fold=s=>s.replace(/đ/g,'d').replace(/Đ/g,'D').normalize('NFD').replace(/[\u0300-\u036f]/g,'').replace(/[^\x20-\x7E]/g,'').replace(/["\\]/g,'').trim().slice(0,23);
const form=o=>Object.entries(o).map(([k,v])=>k+'='+encodeURIComponent(v)).join('&');
async function post(path,o){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form(o)});if(!r.ok)throw 0;return r.json()}
function msg(m,bad){$('#msg').textContent=m;$('#msg').className=bad?'no':''}
function texts(){const t=L();document.documentElement.lang=S.lang;document.title=t.h1;$('#h1').textContent=t.h1;$('#kW').textContent=t.kW;$('#kT').textContent=t.kT;$('#lT').textContent=t.lT;
  $('#q').placeholder=t.ph;$('#go').textContent=t.go;$('#aT').textContent=t.aT;$('#cL').textContent=t.cL;$('#tL').textContent=t.tL;$('#auto').textContent=t.auto;
  $('#vW').textContent=S.place||'-';$('#vT').textContent=S.label||S.tz||'-';
  document.querySelectorAll('.seg button').forEach(b=>b.className=b.dataset.l===S.lang?'on':'')}
function fillCountries(sel){
  const cur=sel||$('#cc').value;const list=D.c.slice().sort((a,b)=>cname(a).localeCompare(cname(b),S.lang));
  $('#cc').textContent='';for(const c of list){const o=document.createElement('option');o.value=c[0];o.textContent=cname(c);$('#cc').append(o)}
  if(cur)$('#cc').value=cur;fillCities();
}
function fillCities(sel){
  const c=D.c.find(x=>x[0]===$('#cc').value);$('#city').textContent='';
  for(const [name,r] of c[3]){const o=document.createElement('option');o.value=r;o.textContent=name;o.dataset.name=name;$('#city').append(o)}
  if(sel!==undefined){const i=[...$('#city').options].findIndex(o=>o.dataset.name===sel);if(i>=0)$('#city').selectedIndex=i}
}
function detect(){const z=Intl.DateTimeFormat().resolvedOptions().timeZone;if(!z||!D)return null;const h=D.z[z]||D.z[D.a[z]];if(!h)return null;return h[0]==='VN'?['VN','Hanoi']:h}
function choose(h){$('#cc').value=h[0];fillCities(h[1])}
async function saveTz(){      // time zone of the selected country/city, saved at once
  const c=D.c.find(x=>x[0]===$('#cc').value),o=$('#city').selectedOptions[0];
  let label=o.dataset.name+' ('+cname(c)+')';if(new TextEncoder().encode(label).length>47)label=o.dataset.name;
  S=await post('/api/settings',{lang:S.lang,tz:D.r[+o.value],label});texts();return label;
}
async function search(){
  const q=$('#q').value.trim();const ul=$('#res');
  if(q.length<2){ul.textContent='';return}
  try{const r=await fetch('https://geocoding-api.open-meteo.com/v1/search?count=6&format=json&language='+S.lang+'&name='+encodeURIComponent(q));R=(await r.json()).results||[]}
  catch(e){msg(L().off,true);return}
  msg('');ul.textContent='';
  if(!R.length){msg(L().none,true);return}
  R.forEach((x,i)=>{const li=document.createElement('li'),b=document.createElement('button'),s=document.createElement('small');b.type='button';
    b.textContent=x.name;s.textContent=[x.admin1,x.country].filter(Boolean).join(', ');b.append(s);b.onclick=()=>pick(i);li.append(b);ul.append(li)});
}
async function pick(i){
  const x=R[i];if(!x)return;const name=fold(x.name)||fold($('#q').value)||'Town';
  try{
    S=await post('/api/place',{place:name,lat:x.latitude.toFixed(3),lon:x.longitude.toFixed(3)});texts();
    let hit=x.timezone&&D?(D.z[x.timezone]||D.z[D.a[x.timezone]]):null;if(hit&&hit[0]==='VN')hit=['VN','Hanoi'];
    if(hit){choose(hit);await saveTz();msg(L().both(name))}else msg(L().wx(name));
    $('#res').textContent='';$('#q').value='';
  }catch(e){msg(L().err,true)}
}
$('#f').onsubmit=e=>{e.preventDefault();clearTimeout(timer);search()};
$('#q').oninput=()=>{clearTimeout(timer);timer=setTimeout(search,450)};
$('#cc').onchange=()=>{fillCities();saveTz().then(l=>msg(L().tz(l))).catch(()=>msg(L().err,true))};
$('#city').onchange=()=>saveTz().then(l=>msg(L().tz(l))).catch(()=>msg(L().err,true));
$('#auto').onclick=()=>{const d=detect();if(d){choose(d);saveTz().then(l=>msg(L().tz(l))).catch(()=>msg(L().err,true))}};
document.querySelectorAll('.seg button').forEach(b=>b.onclick=async()=>{
  try{S=await post('/api/settings',{lang:b.dataset.l,tz:S.tz,label:S.label});texts();if(D)fillCountries();msg(L().lang)}catch(e){msg(L().err,true)}});
(async()=>{
  try{S=await (await fetch('/api/settings')).json()}catch(e){}
  S.lang=S.lang==='fr'?'fr':'en';texts();
  try{D=await (await fetch('/api/timezones')).json()}catch(e){return}
  fillCountries();
  for(const c of D.c)for(const [name,r] of c[3])if(D.r[r]===S.tz&&(S.label===name+' ('+c[1]+')'||S.label===name+' ('+c[2]+')'))choose([c[0],name]);
})();
</script></body></html>)HTML";

const char RADIOS_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Desk Watch radios</title>
<style>
:root{--bg:#10151d;--card:#18202b;--ink:#e8edf5;--muted:#9aa6b8;--line:#2a3544;--acc:#4da3ff;--ok:#7ee0a0;--bad:#ff8a80}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:17px/1.45 system-ui,sans-serif;padding:16px;display:flex;justify-content:center}
main{width:100%;max-width:560px;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:20px}
h1{margin:0 0 6px;font-size:1.4rem}
p.in{margin:0 0 6px;color:var(--muted);font-size:.95rem}
.slot{border:1px solid var(--line);border-radius:10px;padding:12px;margin:14px 0}
.hd{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}.hd b{font-size:1.05rem}.slot input{margin-top:8px}.slot input:first-of-type{margin-top:0}
button:disabled{opacity:.45;cursor:default}
.st:empty{display:none}
label{display:block;margin:8px 0 3px;color:var(--muted);font-size:.85rem}
input{font:inherit;width:100%;padding:10px;border-radius:8px;border:1px solid var(--line);background:#0e131a;color:var(--ink)}
input.bad{border-color:var(--bad)}
.line{display:flex;gap:8px}.line input{flex:1;min-width:0}
button{font:inherit;padding:10px 14px;border-radius:8px;border:1px solid var(--acc);background:transparent;color:var(--acc);cursor:pointer}
button.main{background:var(--acc);color:#04121f;font-weight:700;border:0;width:100%;margin-top:6px}
button.ghost{width:100%;margin-top:10px}
.t{white-space:nowrap}
.st{min-height:1.2em;margin:4px 0 0;font-size:.85rem;color:var(--muted)}
.st.ok{color:var(--ok)}.st.no{color:var(--bad)}
#msg{min-height:1.4em;margin:12px 0 0;color:var(--ok)} #msg.no{color:var(--bad)}
details{margin-top:16px;color:var(--muted);font-size:.92rem} summary{cursor:pointer;color:var(--ink)}
li{margin:6px 0}
input,button{min-height:44px}
@media(max-width:400px){body{padding:10px}main{padding:16px 14px}.slot{padding:10px}}
@media(min-width:680px){body{padding:32px 16px}main{max-width:680px;padding:28px 32px}h1{font-size:1.7rem}}
@media(min-width:1000px){main{max-width:1100px}#slots{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin:14px 0}.slot{margin:0}.main,.ghost{max-width:420px;display:block;margin-left:auto;margin-right:auto}}
</style></head><body><main>
<h1 id="h1"></h1>
<p class="in" id="intro"></p>
<div id="slots"></div>
<button class="main" id="save" type="button"></button>
<button class="ghost" id="reset" type="button"></button>
<p id="msg" role="status"></p>
<details><summary id="hT"></summary><ul id="help"></ul></details>
</main>
<script>
const $=s=>document.querySelector(s);
const TX={en:{h1:'Desk Watch radios',intro:'Three web radios for the Radio page and for the alarm. Changes are saved on the watch.',slot:'Radio ',name:'Name (16 characters at most)',url:'Stream address, starting with http://',test:'Test',
  stop:'Stop',save:'Save the radios',reset:'Factory radios',hT:'How to find a good stream address',
  help:['It must start with http:// (https:// does not fit in the memory of the watch). If the radio gives https://, try the same address with http://.','It must be the direct sound, in MP3 (not AAC, not Ogg), not a web page and not a playlist file (.m3u or .pls): open such a file in a text editor, the real address is inside.','Radio websites call it "stream URL" or "direct link". Prefer 64 to 128 kbps.','The Test button plays it in this browser: if you hear it, the address is good (a browser also plays AAC, which the watch cannot).','Press LISTEN on the watch to be sure; the Serial Monitor says "RADIO connected" or why it failed.'],
  e_name:'Name: 1 to 16 characters, no accents, no quotes.',e_http:'The address must start with http:// (not https://).',e_url:'Invalid address (no space, 159 characters at most).',
  saved:'Saved. If the radio was playing, it has been stopped: press LISTEN again.',err:'The watch does not answer. Check the Wi-Fi.',playing:'Plays in the browser.',fail:'Cannot play: wrong address, https, or an unsupported format.',wait:'Connecting...',confirm:'Put back the three factory radios?',reset_ok:'Factory radios restored.'},
 fr:{h1:'Radios de la Desk Watch',intro:'Trois radios web pour la page Radio et pour le réveil. Les changements sont enregistrés dans la montre.',slot:'Radio ',name:'Nom (16 caractères au plus)',url:'Adresse du flux, commençant par http://',test:'Tester',
  stop:'Arrêter',save:'Enregistrer les radios',reset:'Radios d\u2019origine',hT:'Comment trouver une bonne adresse de flux',
  help:['Elle doit commencer par http:// (le https:// ne tient pas dans la mémoire de la montre). Si la radio donne du https://, essayez la même adresse en http://.','Ce doit être le son direct, en MP3 (pas AAC, pas Ogg), ni une page web ni un fichier de playlist (.m3u ou .pls) : ouvrez un tel fichier dans un éditeur de texte, la vraie adresse est dedans.','Les sites de radios l\u2019appellent « stream URL » ou « lien direct ». Préférez 64 à 128 kbit/s.','Le bouton Tester la joue dans ce navigateur : si vous l\u2019entendez, l\u2019adresse est bonne (un navigateur lit aussi l\u2019AAC, que la montre ne sait pas lire).','Appuyez sur ÉCOUTER sur la montre pour être sûr ; le moniteur série indique « RADIO connected » ou la raison de l\u2019échec.'],
  e_name:'Nom : 1 à 16 caractères, sans accent ni guillemet.',e_http:'L\u2019adresse doit commencer par http:// (pas https://).',e_url:'Adresse invalide (sans espace, 159 caractères au plus).',
  saved:'Enregistré. Si la radio jouait, elle a été arrêtée : appuyez à nouveau sur ÉCOUTER.',err:'La montre ne répond pas. Vérifiez le Wi-Fi.',playing:'Lecture dans le navigateur.',fail:'Lecture impossible : mauvaise adresse, https, ou format non pris en charge.',wait:'Connexion...',confirm:'Remettre les trois radios d\u2019origine ?',reset_ok:'Radios d\u2019origine rétablies.'}};
let lang=(navigator.language||'en').toLowerCase().startsWith('fr')?'fr':'en',data=[],audio=null,playingIdx=-1;
const T=()=>TX[lang];
function msg(m,bad){const e=$('#msg');e.textContent=m;e.className=bad?'no':''}
function stopAudio(){if(audio){audio.pause();audio.removeAttribute('src');audio.load();audio=null}playingIdx=-1}
function build(){
  const t=T();document.documentElement.lang=lang;document.title=t.h1;$('#h1').textContent=t.h1;$('#intro').textContent=t.intro;$('#save').textContent=t.save;$('#reset').textContent=t.reset;$('#hT').textContent=t.hT;
  $('#help').textContent='';for(const h of t.help){const li=document.createElement('li');li.textContent=h;$('#help').append(li)}
  const box=$('#slots');box.textContent='';
  data.forEach((r,i)=>{
    const d=document.createElement('div');d.className='slot';
    d.innerHTML='<div class="hd"><b></b><button class="t" type="button"></button></div><input maxlength="16" autocomplete="off" spellcheck="false"><input maxlength="159" inputmode="url" autocomplete="off" autocapitalize="off" spellcheck="false"><p class="st"></p>';
    const [hd,n,u,st]=[d.children[0],d.children[1],d.children[2],d.children[3]];
    hd.children[0].textContent=t.slot+(i+1);const tb=hd.children[1];tb.textContent=t.test;tb.onclick=()=>test(i,tb);
    n.value=r.name;n.id='n'+i;n.placeholder=t.name;u.value=r.url;u.id='u'+i;u.placeholder=t.url;st.id='s'+i;
    n.oninput=u.oninput=()=>{$('#save').disabled=false;msg('')};
    box.append(d);
  });
}
function test(i,btn){
  const st=$('#s'+i);
  if(playingIdx===i){stopAudio();document.querySelectorAll('.t').forEach(b=>b.textContent=T().test);st.textContent='';st.className='st';return}
  stopAudio();document.querySelectorAll('.t').forEach(b=>b.textContent=T().test);document.querySelectorAll('.st').forEach(s=>{s.textContent='';s.className='st'});
  const url=$('#u'+i).value.trim();
  if(!/^http:\/\/\S+$/.test(url)){st.textContent=T().e_http;st.className='st no';return}
  audio=new Audio();playingIdx=i;st.textContent=T().wait;btn.textContent=T().stop;
  audio.onplaying=()=>{st.textContent=T().playing;st.className='st ok'};
  audio.onerror=()=>{st.textContent=T().fail;st.className='st no';btn.textContent=T().test;playingIdx=-1};
  audio.src=url;audio.play().catch(()=>{});
}
function read(){
  const rows=[];let ok=true;
  data.forEach((r,i)=>{
    const n=$('#n'+i),u=$('#u'+i);n.className=u.className='';
    const name=n.value.trim(),url=u.value.trim();
    if(!/^[\x20-\x7E]{1,16}$/.test(name)||/["\\]/.test(name)){n.className='bad';msg(T().e_name,true);ok=false}
    else if(!/^http:\/\//.test(url)){u.className='bad';msg(T().e_http,true);ok=false}
    else if(!/^[\x21-\x7E]{8,159}$/.test(url)||/["\\]/.test(url)){u.className='bad';msg(T().e_url,true);ok=false}
    rows.push([name,url]);
  });
  return ok?rows:null;
}
async function post(body){
  const r=await fetch('/api/radios',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(!r.ok){const e=await r.json().catch(()=>({}));throw e}
  return r.json();
}
$('#save').onclick=async()=>{
  const rows=read();if(!rows)return;stopAudio();
  try{data=(await post(rows.map((r,i)=>'n'+i+'='+encodeURIComponent(r[0])+'&u'+i+'='+encodeURIComponent(r[1])).join('&'))).radios;build();$('#save').disabled=true;msg(T().saved)}
  catch(e){msg(e&&e.error==='name'?T().e_name:e&&e.error==='http'?T().e_http:e&&e.error?T().e_url:T().err,true)}
};
$('#reset').onclick=async()=>{
  if(!confirm(T().confirm))return;stopAudio();
  try{data=(await post('reset=1')).radios;build();$('#save').disabled=true;msg(T().reset_ok)}catch(e){msg(T().err,true)}
};
(async()=>{
  try{const s=await (await fetch('/api/settings')).json();lang=s.lang==='fr'?'fr':'en'}catch(e){}
  try{data=(await (await fetch('/api/radios')).json()).radios;build();$('#save').disabled=true}catch(e){msg(T().err,true)}
})();
</script></body></html>)HTML";

// Bottom tab bar added to every page (To-Do / Location / Radios). The page is sent in two parts around it.
const char NAV[] PROGMEM = R"HTML(
<style>
body{padding-bottom:calc(84px + env(safe-area-inset-bottom))!important}
#dwnav{position:fixed;left:0;right:0;bottom:0;display:flex;justify-content:center;gap:8px;padding:10px 12px calc(10px + env(safe-area-inset-bottom));background:#0b0f15f2;border-top:1px solid #2a3544;z-index:9}
#dwnav a{flex:1;max-width:200px;min-height:44px;display:flex;align-items:center;justify-content:center;padding:6px;border-radius:10px;border:1px solid #2a3544;color:#e8edf5;text-decoration:none;font:600 15px system-ui,sans-serif;background:#18202b}
#dwnav a.on{background:#4da3ff;color:#04121f;border-color:#4da3ff}
</style>
<nav id="dwnav"><a href="/" data-p="/"></a><a href="/location" data-p="/location"></a><a href="/radios" data-p="/radios"></a></nav>
<script>(()=>{
const N={en:['To-Do','Location','Radios'],fr:['À faire','Lieu','Radios']};
const set=l=>document.querySelectorAll('#dwnav a').forEach((a,i)=>{a.textContent=N[l][i];if(a.dataset.p===(location.pathname==='/settings'?'/location':location.pathname))a.className='on'});
set((navigator.language||'en').toLowerCase().startsWith('fr')?'fr':'en');
fetch('/api/settings').then(r=>r.json()).then(s=>set(s.lang==='fr'?'fr':'en')).catch(()=>{});
})();</script>
)HTML";
void sendPage(const char* page) {
  const char* end = strstr(page, "</body>");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  if (!end) { server.sendContent_P(page); return; }
  server.sendContent_P(page, end - page);
  server.sendContent_P(NAV);
  server.sendContent_P(end);
  server.sendContent("");
}

void sendList(int code = 200) {
  static char json[TASK_JSON_MAX];
  if (!tasks->toJson(json, sizeof json)) { server.send(500, "application/json", "{\"error\":\"internal\"}"); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", json);
}
void sendError(int code, const char* name) {
  String body = String("{\"error\":\"") + name + "\"}";
  server.send(code, "application/json", body);
}
void modified() { if (changed) changed(); sendList(); }
bool idArg(uint16_t& id) {
  const long value = server.arg("id").toInt();
  if (value < 1 || value > 65535) return false;
  id = static_cast<uint16_t>(value); return true;
}
void sendSettings() {
  String body = String("{\"lang\":\"") + (config->lang == 1 ? "fr" : "en") + "\",\"tz\":\"" + config->tz + "\",\"label\":\"";
  for (const char* c = config->tzLabel; *c; ++c) { if (*c == '"' || *c == '\\') body += '\\'; body += *c; }
  body += "\",\"place\":\""; body += config->wPlace;                       // wPlace never holds quotes or backslashes (checked on input)
  body += "\",\"lat\":"; body += String(config->wLat, 3); body += ",\"lon\":"; body += String(config->wLon, 3); body += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}
void sendRadios() {
  String body = "{\"radios\":[";
  for (int i = 0; i < RADIO_COUNT; ++i) { if (i) body += ','; body += "{\"name\":\""; body += RADIOS[i].name; body += "\",\"url\":\""; body += RADIOS[i].url; body += "\"}"; }
  body += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}
bool labelValid(const String& l) {
  if (l.length() == 0 || l.length() >= sizeof config->tzLabel) return false;
  for (size_t i = 0; i < l.length(); ++i) if (static_cast<uint8_t>(l[i]) < 0x20) return false;
  return true;
}
void routes() {
  server.on("/", HTTP_GET, [] { sendPage(PAGE); });
  server.on("/location", HTTP_GET, [] { sendPage(SETTINGS_PAGE); });
  server.on("/radios", HTTP_GET, [] { sendPage(RADIOS_PAGE); });
  server.on("/api/radios", HTTP_GET, [] { sendRadios(); });
  server.on("/api/radios", HTTP_POST, [] {
    if (server.arg("reset") == "1") {
      if (radioActive()) radioStop();
      radioDefaults(); if (radiosChanged) radiosChanged(); sendRadios(); return;
    }
    for (int i = 0; i < RADIO_COUNT; ++i) {                      // check all three before changing anything
      const String n = server.arg(String("n") + i), u = server.arg(String("u") + i);
      if (!radioValid(n.c_str(), u.c_str())) {
        const bool http = u.startsWith("http://") && u.length() > 7;
        sendError(400, n.length() < 1 || n.length() > RADIO_NAME_MAX || !radioValid(n.c_str(), "http://x") ? "name" : (http ? "url" : "http")); return;
      }
    }
    if (radioActive()) radioStop();                              // never rewrite an address the decoder may be reading
    for (int i = 0; i < RADIO_COUNT; ++i) radioSet(i, server.arg(String("n") + i).c_str(), server.arg(String("u") + i).c_str());
    if (radiosChanged) radiosChanged();
    sendRadios();
  });
  server.on("/api/place", HTTP_POST, [] {
    String place = server.arg("place"); const float lat = server.arg("lat").toFloat(), lon = server.arg("lon").toFloat();
    place.trim();
    bool ok = place.length() >= 1 && place.length() < sizeof config->wPlace && server.arg("lat").length() && server.arg("lon").length() && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
    for (size_t i = 0; ok && i < place.length(); ++i) { const uint8_t c = place[i]; if (c < 0x20 || c > 0x7E || c == '"' || c == '\\') ok = false; }
    if (!ok) { sendError(400, "place"); return; }
    strlcpy(config->wPlace, place.c_str(), sizeof config->wPlace); config->wLat = lat; config->wLon = lon;
    if (settingsChanged) settingsChanged();
    sendSettings();
  });
  server.on("/api/tasks", HTTP_GET, [] { sendList(); });
  server.on("/api/add", HTTP_POST, [] {
    if (tasks->count() >= TASK_MAX) { sendError(409, "full"); return; }
    if (!tasks->add(server.arg("text").c_str())) { sendError(400, "invalid"); return; }
    modified();
  });
  server.on("/api/toggle", HTTP_POST, [] {
    uint16_t id; if (!idArg(id) || !tasks->toggle(id)) { sendError(404, "unknown"); return; }
    modified();
  });
  server.on("/api/delete", HTTP_POST, [] {
    uint16_t id; if (!idArg(id) || !tasks->remove(id)) { sendError(404, "unknown"); return; }
    modified();
  });
  server.on("/api/clear", HTTP_POST, [] { if (tasks->clearDone()) { if (changed) changed(); } sendList(); });
  server.on("/settings", HTTP_GET, [] { sendPage(SETTINGS_PAGE); });                // old address
  server.on("/api/settings", HTTP_GET, [] { sendSettings(); });
  server.on("/api/settings", HTTP_POST, [] {
    const String lang = server.arg("lang"), tz = server.arg("tz"), label = server.arg("label");
    if (lang != "en" && lang != "fr") { sendError(400, "lang"); return; }
    if (!tzRuleValid(tz.c_str()) || !labelValid(label)) { sendError(400, "tz"); return; }
    config->lang = lang == "fr" ? 1 : 0;
    strlcpy(config->tz, tz.c_str(), sizeof config->tz);
    strlcpy(config->tzLabel, label.c_str(), sizeof config->tzLabel);
    if (settingsChanged) settingsChanged();
    sendSettings();
  });
  server.on("/api/timezones", HTTP_GET, [] {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.send_P(200, "application/json", reinterpret_cast<const char*>(TZ_JSON_GZ), TZ_JSON_GZ_LEN);
  });
  server.onNotFound([] { sendError(404, "not_found"); });
}
}  // namespace

void beginTaskWeb(TaskStore& store, void (*onChange)(), Config& cfg, void (*onSettings)(), void (*onRadios)()) { tasks = &store; changed = onChange; config = &cfg; settingsChanged = onSettings; radiosChanged = onRadios; }

void handleTaskWeb() {
  if (!tasks || WiFi.status() != WL_CONNECTED) return;
  if (!started) {
    routes(); server.begin();
    if (MDNS.begin(TASK_HOSTNAME)) MDNS.addService("http", "tcp", 80);
    started = true;
    Serial.printf("WEB http://%s.local/ (To-Do, /location, /radios) and http://%s/\n", TASK_HOSTNAME, WiFi.localIP().toString().c_str());
  }
  server.handleClient();
}
