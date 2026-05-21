// shared.js — funzioni comuni a tutte le pagine FurbyMind

function navOpen(){document.getElementById('nav-drawer').classList.add('open');document.getElementById('nav-overlay').classList.add('open');}
function navClose(){document.getElementById('nav-drawer').classList.remove('open');document.getElementById('nav-overlay').classList.remove('open');}

function fmtSec(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;return(h?h+'h ':'')+m+'m '+(ss<10?'0':'')+ss+'s';}
function fmtKB(kb){return kb>=1024?(kb/1024).toFixed(1)+' MB':kb+' KB';}

// Aggiorna il sys-bar e calcola l'uptime client-side dopo il primo fetch.
// bootRef = { t: Date.now(), s: uptime_s } — impostato al primo /sys/info
var _bootRef = null;

function _uptimeTick(){
  if(!_bootRef)return;
  var el=document.getElementById('esp-uptime');
  if(el)el.textContent=fmtSec(_bootRef.s+Math.floor((Date.now()-_bootRef.t)/1000));
}

function _updateSysBar(d){
  var sd=d.sd_total?' | SD '+fmtKB(d.sd_used)+'/'+fmtKB(d.sd_total):'';
  var flashUsed=d.flash_used_kb!=null?fmtKB(d.flash_used_kb):'?';
  var flashFree=d.flash_free_kb!=null?fmtKB(d.flash_free_kb):'?';
  var bar=document.getElementById('sys-bar')||document.getElementById('sys-info-bar');
  if(bar)bar.textContent='CPU '+d.cpu_mhz+'MHz'
    +' | Heap '+fmtKB(d.heap_free)+'/'+fmtKB(d.heap_total)
    +' | PSRAM '+fmtKB(d.psram_free)+'/'+fmtKB(d.psram_total)
    +' | Sketch '+fmtKB(d.sketch_used)+'/'+fmtKB(d.sketch_total)
    +' | SPIFFS '+fmtKB(d.spiffs_used)+'/'+fmtKB(d.spiffs_total)
    +' | Flash '+flashUsed+' usati, '+flashFree+' liberi ('+d.flash_mb+'MB)'+sd;
  if(d.uptime_s!=null&&!_bootRef){
    _bootRef={t:Date.now(),s:d.uptime_s};
    setInterval(_uptimeTick,1000);
    _uptimeTick();
  }
}

document.addEventListener('DOMContentLoaded',function(){
  var ipEl=document.getElementById('esp-ip');
  if(ipEl)ipEl.textContent=window.location.hostname;
  (function sysLoop(){
    fetch('/sys/info').then(function(r){return r.json();}).then(_updateSysBar).catch(function(){});
    setTimeout(sysLoop,60000);
  })();
});
