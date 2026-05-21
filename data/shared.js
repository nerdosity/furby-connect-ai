// shared.js — funzioni comuni a tutte le pagine FurbyMind

function navOpen(){document.getElementById('nav-drawer').classList.add('open');document.getElementById('nav-overlay').classList.add('open');}
function navClose(){document.getElementById('nav-drawer').classList.remove('open');document.getElementById('nav-overlay').classList.remove('open');}

function fmtSec(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;return(h?h+'h ':'')+m+'m '+(ss<10?'0':'')+ss+'s';}
function fmtKB(kb){return kb>=1024?(kb/1024).toFixed(1)+' MB':kb+' KB';}

// ── i18n ──────────────────────────────────────────────────────────────────────

var _SHARED_TR={
  it:{
    'nav.config':'Configurazione','nav.debug':'Debug Furby','nav.camera':'Camera','nav.reset':'Reset ESP32',
    'nav.confirm_reset':'Riavviare l\'ESP32?',
    'sys.loading':'carico...',
    // index.html
    'wifi.title':'Rete Wi-Fi','wifi.connected':'Connesso: ','wifi.disconnected':'Non connesso',
    'wifi.col_prio':'Priorità','wifi.col_ssid':'SSID','wifi.active':'attiva',
    'wifi.no_nets':'Nessuna rete salvata',
    'wifi.btn_connect':'⚡ Connetti ora','wifi.btn_scan':'🔍 Scansiona',
    'wifi.label_ssid':'SSID','wifi.label_pass':'Password',
    'wifi.label_prio':'Priorità (0 = massima)',
    'wifi.btn_add':'+ Aggiungi / Aggiorna rete',
    'wifi.scan_wait':'⌛ Scansione in corso (~2s)...','wifi.scan_none':'Nessuna rete trovata.',
    'wifi.scan_err':'Errore scansione.','wifi.scan_use':'Usa',
    'wifi.open':'Aperta',
    'llm.title':'Configurazione LLM',
    'llm.label_prov':'Fornitore','llm.label_model':'Modello ',
    'llm.label_key':'Token API',
    'llm.btn_save':'Salva configurazione AI','llm.btn_test':'🧪 Test',
    'llm.test_running':'Test in corso...','llm.test_ok':'✓ OK — ','llm.test_err':'✗ Errore: ','llm.test_timeout':'✗ Timeout/errore',
    'llm.models_loading':'carico...','llm.models_no_key':'token non salvato — lista predefinita',
    'llm.models_none':'nessun modello — lista predefinita','llm.models_offline':'offline — lista predefinita',
    'llm.models_count':' disponibili',
    'el.title':'ElevenLabs TTS',
    'el.label_key':'Token API ElevenLabs','el.label_voice':'Voce',
    'el.label_fmt':'Formato audio',
    'el.btn_save':'Salva ElevenLabs','el.btn_test':'🧪 Test',
    'el.hint_mp3':'MP3 22kHz 32kbps — funziona con account free, ~4x più leggero in cache',
    'el.hint_pcm':'PCM 16kHz mono — qualità alta, solo account Pro',
    'sd.title':'Scheda SD','sd.present':'SD presente','sd.absent':'SD assente',
    'sd.absent_sub':'Modalità RAM streaming','sd.btn_format':'🗑 Inizializza / Reset cache SD',
    'sd.format_confirm':'Cancellare tutta la cache SD?',
    'vad.title':'Microfoni / Rilevamento voce',
    'vad.label_thresh':'Soglia sensibilità (RMS 0–32767, default 800)',
    'vad.btn_en':'🎤 Abilita','vad.btn_dis':'🔇 Disabilita',
    'vad.active':'VAD attivo','vad.disabled':'VAD disabilitato',
    'stt.desc':'Trascrive il parlato con Whisper prima di passarlo all\'LLM. Richiede chiave OpenAI. +~1-2s di latenza.',
    'stt.btn_en':'Abilita STT','stt.btn_dis':'Disabilita STT',
    'stt.active':'STT (Whisper) attivo','stt.disabled':'STT (Whisper) disabilitato',
    'ble.title':'Bluetooth Furby',
    'ble.connected':'Connesso','ble.connecting':'Connessione...','ble.scanning':'Scansione...',
    'ble.not_connected':'Non connesso',
    'ble.scan':'🔍 Cerca Furby','ble.stop':'⏹ Stop scan','ble.disconnect':'⛔ Disconnetti',
    'ble.scanning_msg':'⌛ Scansione in corso...',
    'ble.found_so_far':'Trovati finora:','ble.furby_found':'Furby trovati:',
    'ble.connect':'Connetti',
    'ble.label_svc':'Service UUID','ble.label_char':'Characteristic UUID TX',
    'ble.btn_save':'Salva UUID',
    'ble.connected_for':'connesso da: ',
    'wifi.connect_err':'Errore di rete',
    // camera.html
    'cam.streaming':'Streaming MJPEG attivo','cam.paused':'In pausa',
    'cam.pause':'⏸ Pausa','cam.resume':'▶ Riprendi',
    'cam.quality_high':'Qualità alta','cam.quality_low':'Qualità bassa',
    'cam.describe_title':'🧠 Descrivi frame con LLM',
    'cam.describe_hint':'Cattura un frame e chiedi all\'LLM configurato di descriverlo con il prompt qui sotto.',
    'cam.save_prompt':'💾 Salva prompt','cam.describe_now':'🔍 Descrivi ora',
    'cam.tts_voice':'🔊 Output vocale',
    'cam.tts_browser':'🖥 Browser','cam.tts_board':'📡 Board',
    'cam.tts_hint':'Browser: Web Speech API — Board: ElevenLabs via ESP32',
    'cam.saved':'Salvato.','cam.error':'Errore.',
    'cam.sending':'Analisi in corso...','cam.error_conn':'Errore connessione.',
    'cam.tts_sending':'Invio a ElevenLabs...','cam.tts_playing':'In riproduzione sulla board.',
    'cam.tts_err':'Errore TTS: ','cam.speech_unsupported':'Web Speech API non supportata in questo browser.',
    'cam.reset_confirm':'Riavviare l\'ESP32?',
    // wifi_connect.html
    'wc.connecting':'Connessione in corso…','wc.connected':'Connesso!','wc.failed':'Connessione fallita',
    'wc.back':'Torna alla home'
  },
  en:{
    'nav.config':'Configuration','nav.debug':'Debug Furby','nav.camera':'Camera','nav.reset':'Reset ESP32',
    'nav.confirm_reset':'Restart the ESP32?',
    'sys.loading':'loading...',
    // index.html
    'wifi.title':'Wi-Fi Network','wifi.connected':'Connected: ','wifi.disconnected':'Not connected',
    'wifi.col_prio':'Priority','wifi.col_ssid':'SSID','wifi.active':'active',
    'wifi.no_nets':'No saved networks',
    'wifi.btn_connect':'⚡ Connect now','wifi.btn_scan':'🔍 Scan',
    'wifi.label_ssid':'SSID','wifi.label_pass':'Password',
    'wifi.label_prio':'Priority (0 = highest)',
    'wifi.btn_add':'+ Add / Update network',
    'wifi.scan_wait':'⌛ Scanning (~2s)...','wifi.scan_none':'No networks found.',
    'wifi.scan_err':'Scan error.','wifi.scan_use':'Use',
    'wifi.open':'Open',
    'llm.title':'LLM Configuration',
    'llm.label_prov':'Provider','llm.label_model':'Model ',
    'llm.label_key':'API Token',
    'llm.btn_save':'Save AI configuration','llm.btn_test':'🧪 Test',
    'llm.test_running':'Testing...','llm.test_ok':'✓ OK — ','llm.test_err':'✗ Error: ','llm.test_timeout':'✗ Timeout/error',
    'llm.models_loading':'loading...','llm.models_no_key':'token not saved — default list',
    'llm.models_none':'no models — default list','llm.models_offline':'offline — default list',
    'llm.models_count':' available',
    'el.title':'ElevenLabs TTS',
    'el.label_key':'ElevenLabs API Token','el.label_voice':'Voice',
    'el.label_fmt':'Audio format',
    'el.btn_save':'Save ElevenLabs','el.btn_test':'🧪 Test',
    'el.hint_mp3':'MP3 22kHz 32kbps — works with free account, ~4x lighter in cache',
    'el.hint_pcm':'PCM 16kHz mono — high quality, Pro account only',
    'sd.title':'SD Card','sd.present':'SD present','sd.absent':'SD absent',
    'sd.absent_sub':'RAM streaming mode','sd.btn_format':'🗑 Initialize / Reset SD cache',
    'sd.format_confirm':'Delete all SD cache?',
    'vad.title':'Microphones / Voice detection',
    'vad.label_thresh':'Sensitivity threshold (RMS 0–32767, default 800)',
    'vad.btn_en':'🎤 Enable','vad.btn_dis':'🔇 Disable',
    'vad.active':'VAD active','vad.disabled':'VAD disabled',
    'stt.desc':'Transcribes speech with Whisper before passing it to the LLM. Requires OpenAI key. +~1-2s latency.',
    'stt.btn_en':'Enable STT','stt.btn_dis':'Disable STT',
    'stt.active':'STT (Whisper) active','stt.disabled':'STT (Whisper) disabled',
    'ble.title':'Bluetooth Furby',
    'ble.connected':'Connected','ble.connecting':'Connecting...','ble.scanning':'Scanning...',
    'ble.not_connected':'Not connected',
    'ble.scan':'🔍 Find Furby','ble.stop':'⏹ Stop scan','ble.disconnect':'⛔ Disconnect',
    'ble.scanning_msg':'⌛ Scanning...',
    'ble.found_so_far':'Found so far:','ble.furby_found':'Furby found:',
    'ble.connect':'Connect',
    'ble.label_svc':'Service UUID','ble.label_char':'Characteristic UUID TX',
    'ble.btn_save':'Save UUID',
    'ble.connected_for':'connected for: ',
    'wifi.connect_err':'Network error',
    // camera.html
    'cam.streaming':'MJPEG stream active','cam.paused':'Paused',
    'cam.pause':'⏸ Pause','cam.resume':'▶ Resume',
    'cam.quality_high':'High quality','cam.quality_low':'Low quality',
    'cam.describe_title':'🧠 Describe frame with LLM',
    'cam.describe_hint':'Capture a frame and ask the configured LLM to describe it using the prompt below.',
    'cam.save_prompt':'💾 Save prompt','cam.describe_now':'🔍 Describe now',
    'cam.tts_voice':'🔊 Voice output',
    'cam.tts_browser':'🖥 Browser','cam.tts_board':'📡 Board',
    'cam.tts_hint':'Browser: Web Speech API — Board: ElevenLabs via ESP32',
    'cam.saved':'Saved.','cam.error':'Error.',
    'cam.sending':'Analysing...','cam.error_conn':'Connection error.',
    'cam.tts_sending':'Sending to ElevenLabs...','cam.tts_playing':'Playing on board.',
    'cam.tts_err':'TTS error: ','cam.speech_unsupported':'Web Speech API not supported in this browser.',
    'cam.reset_confirm':'Restart the ESP32?',
    // wifi_connect.html
    'wc.connecting':'Connecting…','wc.connected':'Connected!','wc.failed':'Connection failed',
    'wc.back':'Back to home'
  }
};

function T(k){
  var l=localStorage.getItem('furby_lang')||'it';
  return (_SHARED_TR[l]||_SHARED_TR.it)[k]||(_SHARED_TR.it[k]||k);
}

function applyI18n(){
  // se la pagina ha il proprio sistema i18n (debug.html), non interferire
  if(typeof setLang==='function')return;
  document.querySelectorAll('[data-i18n]').forEach(function(el){
    var k=el.getAttribute('data-i18n');
    var attr=el.getAttribute('data-i18n-attr');
    if(attr)el.setAttribute(attr,T(k));
    else el.textContent=T(k);
  });
  // sync language selector
  var sel=document.getElementById('lang-sel-shared');
  if(sel)sel.value=localStorage.getItem('furby_lang')||'it';
}

function setSharedLang(l){
  localStorage.setItem('furby_lang',l);
  applyI18n();
}

// ── system bar ────────────────────────────────────────────────────────────────

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

  applyI18n();

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
