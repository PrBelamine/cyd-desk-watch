#include "TaskWeb.h"
#include "Settings.h"
#include "TimeZones.h"
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

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
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
</style></head><body><main>
<h1 id="h1"></h1>
<p id="count"></p>
<form id="f"><input id="t" type="text" maxlength="60" autocomplete="off"><button id="add"></button></form>
<ul id="l"></ul><p id="empty" hidden></p>
<footer><button class="ghost" id="clr" type="button"></button><a id="set" href="/settings"></a></footer>
<p id="msg" role="status"></p></main>
<script>
const $=s=>document.querySelector(s);
const TX={en:{title:'Digital Post-it',ph:'New task',add:'Add',clr:'Delete finished tasks',set:'Settings',load:'Loading…',empty:'Nothing to do right now.',none:'Empty list',done:'Done',del:'Delete',
 cnt:(k,n,f)=>k+' of '+n+' done · '+f+' free slot'+(f===1?'':'s'),full:'List full: 18 tasks maximum.',bad:'Empty or invalid text.',off:'The Post-it does not answer. Check the Wi-Fi.'},
 fr:{title:'Post-it numérique',ph:'Nouvelle tâche',add:'Ajouter',clr:'Supprimer les tâches terminées',set:'Réglages',load:'Chargement…',empty:'Rien à faire pour le moment.',none:'Liste vide',done:'Terminée',del:'Supprimer',
 cnt:(k,n,f)=>k+' sur '+n+' faites · '+f+' place'+(f===1?'':'s')+' libre'+(f===1?'':'s'),full:'Liste pleine : 18 tâches maximum.',bad:'Texte vide ou invalide.',off:'Le Post-it ne répond pas. Vérifiez le Wi-Fi.'}};
let lang=(navigator.language||'en').toLowerCase().startsWith('fr')?'fr':'en',last=null;
const T=()=>TX[lang];
function texts(){document.documentElement.lang=lang;document.title=T().title;$('#h1').textContent=T().title;$('#t').placeholder=T().ph;$('#t').setAttribute('aria-label',T().ph);
  $('#add').textContent=T().add;$('#clr').textContent=T().clr;$('#set').textContent=T().set;$('#empty').textContent=T().empty;if(!last)$('#count').textContent=T().load}
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
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Desk Watch</title>
<style>
:root{--bg:#10151d;--card:#18202b;--ink:#e8edf5;--muted:#9aa6b8;--line:#2a3544;--acc:#4da3ff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:17px/1.45 system-ui,sans-serif;padding:16px;display:flex;justify-content:center}
main{width:100%;max-width:520px;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:20px}
h1{margin:0 0 14px;font-size:1.4rem}
label{display:block;margin:14px 0 4px;color:var(--muted);font-size:.9rem}
select,button{font:inherit;width:100%;padding:11px;border-radius:8px;border:1px solid var(--line);background:#0e131a;color:var(--ink)}
button{background:var(--acc);color:#04121f;font-weight:700;border:0;margin-top:18px;cursor:pointer}
button.ghost{background:transparent;color:var(--acc);border:1px solid var(--acc);margin-top:10px}
a{color:var(--acc)} #msg{min-height:1.4em;margin:12px 0 0;color:#7ee0a0} #hint{color:var(--muted);font-size:.9rem;margin:8px 0 0}
p.cur{margin:0 0 8px;color:var(--muted)}
</style></head><body><main>
<h1 id="h1"></h1>
<p class="cur" id="cur"></p>
<label id="lL" for="lang"></label>
<select id="lang"><option value="en">English</option><option value="fr">Français</option></select>
<label id="cL" for="cc"></label><select id="cc"></select>
<label id="tL" for="city"></label><select id="city"></select>
<p id="hint"></p>
<button class="ghost" id="auto" type="button"></button>
<button id="save" type="button"></button>
<p id="msg" role="status"></p>
<p><a id="back" href="/"></a></p>
</main>
<script>
const $=s=>document.querySelector(s);
const TX={en:{h1:'Desk Watch settings',cur:'Current time zone: ',lang:'Language',cc:'Country',city:'City',auto:'Use the time zone of this device',save:'Save',saved:'Saved. The watch now uses this time zone.',back:'← Post-it',none:'(no city)',det:'Detected on this device: ',err:'Could not save.'},
 fr:{h1:'Réglages de la Desk Watch',cur:'Fuseau horaire actuel : ',lang:'Langue',cc:'Pays',city:'Ville',auto:'Utiliser le fuseau de cet appareil',save:'Enregistrer',saved:'Enregistré. La montre utilise maintenant ce fuseau horaire.',back:'← Post-it',none:'(aucune ville)',det:'Détecté sur cet appareil : ',err:'Enregistrement impossible.'}};
let D=null,S={lang:(navigator.language||'en').toLowerCase().startsWith('fr')?'fr':'en',tz:'',label:''};
const lg=()=>$('#lang').value;
const T=()=>TX[lg()];
const cname=c=>lg()==='fr'?c[2]:c[1];
function texts(){const t=T();document.documentElement.lang=lg();document.title=t.h1;$('#h1').textContent=t.h1;$('#lL').textContent=t.lang;$('#cL').textContent=t.cc;$('#tL').textContent=t.city;
  $('#auto').textContent=t.auto;$('#save').textContent=t.save;$('#back').textContent=t.back;$('#cur').textContent=t.cur+(S.label||S.tz)}
function fillCountries(sel){
  const cur=sel||$('#cc').value;const list=D.c.slice().sort((a,b)=>cname(a).localeCompare(cname(b),lg()));
  $('#cc').textContent='';for(const c of list){const o=document.createElement('option');o.value=c[0];o.textContent=cname(c);$('#cc').append(o)}
  if(cur)$('#cc').value=cur;fillCities();
}
function fillCities(sel){
  const c=D.c.find(x=>x[0]===$('#cc').value);$('#city').textContent='';
  for(const [name,r] of c[3]){const o=document.createElement('option');o.value=r;o.textContent=name;o.dataset.name=name;$('#city').append(o)}
  if(sel!==undefined){const i=[...$('#city').options].findIndex(o=>o.dataset.name===sel);if(i>=0)$('#city').selectedIndex=i}
}
function detect(){
  const z=Intl.DateTimeFormat().resolvedOptions().timeZone;if(!z)return null;
  const hit=D.z[z]||D.z[D.a[z]];if(!hit)return null;
  return hit[0]==='VN'?['VN','Hanoi']:hit;
}
function choose(h){$('#cc').value=h[0];fillCities(h[1])}
async function save(){
  const c=D.c.find(x=>x[0]===$('#cc').value),o=$('#city').selectedOptions[0];
  let label=o.dataset.name+' ('+cname(c)+')';const tz=D.r[+o.value];if(new TextEncoder().encode(label).length>47)label=o.dataset.name;
  const body='lang='+lg()+'&tz='+encodeURIComponent(tz)+'&label='+encodeURIComponent(label);
  try{const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    if(!r.ok)throw 0;S=await r.json();texts();$('#msg').textContent=T().saved}catch(e){$('#msg').textContent=T().err}
}
(async()=>{
  try{S=await (await fetch('/api/settings')).json()}catch(e){}
  $('#lang').value=S.lang==='fr'?'fr':'en';
  D=await (await fetch('/api/timezones')).json();
  texts();fillCountries();
  let found=null;
  for(const c of D.c)for(const [name,r] of c[3])if(D.r[r]===S.tz&&(S.label===name+' ('+c[1]+')'||S.label===name+' ('+c[2]+')'))found=[c[0],name];
  const d=detect();
  if(found)choose(found);else if(d)choose(d);
  if(d){const c=D.c.find(x=>x[0]===d[0]);$('#hint').textContent=T().det+d[1]+' ('+cname(c)+')'}
  $('#lang').onchange=()=>{texts();fillCountries();const f=found;const cur=$('#cc').value;if(cur)fillCities();};
  $('#cc').onchange=()=>fillCities();
  $('#auto').onclick=()=>{const d=detect();if(d)choose(d)};
  $('#save').onclick=save;
})();
</script></body></html>)HTML";

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
  body += "\"}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}
bool labelValid(const String& l) {
  if (l.length() == 0 || l.length() >= sizeof config->tzLabel) return false;
  for (size_t i = 0; i < l.length(); ++i) if (static_cast<uint8_t>(l[i]) < 0x20) return false;
  return true;
}
void routes() {
  server.on("/", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", PAGE); });
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
  server.on("/settings", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", SETTINGS_PAGE); });
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

void beginTaskWeb(TaskStore& store, void (*onChange)(), Config& cfg, void (*onSettings)()) { tasks = &store; changed = onChange; config = &cfg; settingsChanged = onSettings; }

void handleTaskWeb() {
  if (!tasks || WiFi.status() != WL_CONNECTED) return;
  if (!started) {
    routes(); server.begin();
    if (MDNS.begin(TASK_HOSTNAME)) MDNS.addService("http", "tcp", 80);
    started = true;
    Serial.printf("POSTIT web http://%s.local/ and http://%s/\n", TASK_HOSTNAME, WiFi.localIP().toString().c_str());
  }
  server.handleClient();
}
