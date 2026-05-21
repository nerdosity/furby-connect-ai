// shared.js — funzioni comuni a tutte le pagine FurbyMind

function navOpen(){document.getElementById('nav-drawer').classList.add('open');document.getElementById('nav-overlay').classList.add('open');}
function navClose(){document.getElementById('nav-drawer').classList.remove('open');document.getElementById('nav-overlay').classList.remove('open');}

function fmtSec(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;return(h?h+'h ':'')+m+'m '+(ss<10?'0':'')+ss+'s';}
function fmtKB(kb){return kb>=1024?(kb/1024).toFixed(1)+' MB':kb+' KB';}

// _bootRef = { t: timestamp_ms, s: uptime_s } — persiste in sessionStorage tra pagine
var _uptimeTimer = null;
var _fwUpdateAvailable = false;
var _fwCurrent = null;
var _lastSysPlain = '';
var _lastTempC = null;
var _bootRef = (function(){
  try {
    var v=JSON.parse(sessionStorage.getItem('_furby_boot'));
    return (v&&v.t&&v.s!=null)?v:null;
  } catch(e){return null;}
})();

function _bootRefSet(t,s){
  _bootRef={t:t,s:s};
  try{sessionStorage.setItem('_furby_boot',JSON.stringify(_bootRef));}catch(e){}
}

function _uptimeTick(){
  if(!_bootRef)return;
  var el=document.getElementById('esp-uptime');
  if(el)el.textContent=fmtSec(_bootRef.s+Math.floor((Date.now()-_bootRef.t)/1000));
}

function _tempColor(c){
  if(c>=85) return '#ff2222';
  if(c>=75) return '#ff8800';
  if(c>=60) return '#ffcc00';
  return '';
}

function _renderSysBar(bar, plainText, tempC){
  while(bar.firstChild) bar.removeChild(bar.firstChild);
  bar.appendChild(document.createTextNode(plainText));
  if(tempC!=null){
    bar.appendChild(document.createTextNode(' | T '));
    var col=_tempColor(tempC);
    if(col){
      var sp=document.createElement('span');
      sp.style.color=col;
      sp.style.fontWeight='bold';
      sp.textContent=tempC+'°C';
      bar.appendChild(sp);
    } else {
      bar.appendChild(document.createTextNode(tempC+'°C'));
    }
  }
}

function _updateSysBar(d){
  var sd=d.sd_total?' | SD '+fmtKB(d.sd_used)+'/'+fmtKB(d.sd_total):'';
  var flashUsed=d.flash_used_kb!=null?fmtKB(d.flash_used_kb):'?';
  var flashFree=d.flash_free_kb!=null?fmtKB(d.flash_free_kb):'?';
  var bat=d.bat_mv>0?' | BAT '+(d.bat_mv/1000).toFixed(2)+'V':'';
  var bar=document.getElementById('sys-bar')||document.getElementById('sys-info-bar');
  var sramUsed=d.heap_total-d.heap_free;
  var sramPct=d.heap_total?Math.round(sramUsed*100/d.heap_total):0;
  var psramUsed=d.psram_total-d.psram_free;
  var psramPct=d.psram_total?Math.round(psramUsed*100/d.psram_total):0;
  var fw=d.fw_version?' | FW '+d.fw_version+(_fwUpdateAvailable?' ⬆':''):'';
  _lastSysPlain='CPU '+d.cpu_mhz+'MHz'
    +' | SRAM '+fmtKB(sramUsed)+'/'+fmtKB(d.heap_total)+' ('+sramPct+'%)'
    +' | PSRAM '+fmtKB(psramUsed)+'/'+fmtKB(d.psram_total)+' ('+psramPct+'%)'
    +' | Sketch '+fmtKB(d.sketch_used)+'/'+fmtKB(d.sketch_total)
    +' | SPIFFS '+fmtKB(d.spiffs_used)+'/'+fmtKB(d.spiffs_total)
    +' | Flash '+flashUsed+' usati, '+flashFree+' liberi ('+d.flash_mb+'MB)'+bat+sd+fw;
  _lastTempC=(d.chip_temp_c!=null)?d.chip_temp_c:null;
  if(bar) _renderSysBar(bar, _lastSysPlain, _lastTempC);
  if(d.uptime_s!=null&&!_bootRef){
    _bootRefSet(Date.now(),d.uptime_s);
    if(!_uptimeTimer)_uptimeTimer=setInterval(_uptimeTick,1000);
  }
  _uptimeTick();
  if(d.fw_version&&!_fwCurrent){
    _fwCurrent=d.fw_version;
    fetch('https://raw.githubusercontent.com/nerdosity/furby-connect-ai/main/version.txt')
      .then(function(r){return r.text();})
      .then(function(t){
        var latest=t.trim();
        if(latest&&latest!==_fwCurrent){
          _fwUpdateAvailable=true;
          _lastSysPlain=_lastSysPlain.replace(' | FW '+_fwCurrent,' | FW '+_fwCurrent+' ⬆ '+latest);
          var bar=document.getElementById('sys-bar')||document.getElementById('sys-info-bar');
          if(bar) _renderSysBar(bar, _lastSysPlain, _lastTempC);
        }
      }).catch(function(){});
  }
}

document.addEventListener('DOMContentLoaded',function(){
  var ipEl=document.getElementById('esp-ip');
  if(ipEl)ipEl.textContent=window.location.hostname;
  if(_bootRef){_uptimeTimer=setInterval(_uptimeTick,1000);_uptimeTick();}

  var _sysTimer=null;
  function scheduleSys(ms){
    clearTimeout(_sysTimer);
    if(!document.hidden) _sysTimer=setTimeout(sysLoop,ms);
  }
  function sysLoop(){
    if(document.hidden){return;}
    fetch('/sys/info').then(function(r){return r.json();}).then(function(d){
      _updateSysBar(d);
      scheduleSys(60000);
    }).catch(function(){scheduleSys(60000);});
  }
  document.addEventListener('visibilitychange',function(){
    if(!document.hidden) scheduleSys(0);
  });
  sysLoop();
});
