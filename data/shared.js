// shared.js — funzioni comuni a tutte le pagine FurbyMind


function fmtSec(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;return(h?h+'h ':'')+m+'m '+(ss<10?'0':'')+ss+'s';}
function fmtKB(kb){return kb>=1048576?(kb/1048576).toFixed(1)+' GB':kb>=1024?(kb/1024).toFixed(1)+' MB':kb+' KB';}


// ── navbar component ──────────────────────────────────────────────────────────

var _NAV_SVG={
  home:'<path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
  debug:'<circle cx="12" cy="12" r="3"/><path d="M19.07 4.93A10 10 0 0 1 21 12M4.93 4.93A10 10 0 0 0 3 12m9 9a10 10 0 0 0 6.36-2.29M5.64 18.71A10 10 0 0 0 12 21"/>',
  camera:'<path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"/><circle cx="12" cy="13" r="4"/>',
  reset:'<polyline points="1 4 1 10 7 10"/><path d="M3.51 15a9 9 0 1 0 .49-3.5"/>'
};

function _navSvg(key){
  return '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">'+_NAV_SVG[key]+'</svg>';
}

function renderNav(active){
  var pages=[
    {key:'home',  href:'/',       i18n:'nav.config', def:'Configurazione'},
    {key:'debug', href:'/debug',  i18n:'nav.debug',  def:'Debug'},
    {key:'camera',href:'/camera', i18n:'nav.camera', def:'Camera'}
  ];
  var isDebug=(typeof setLang==='function');
  var tFn=isDebug?function(k){return(TRANSLATIONS[gLang]||TRANSLATIONS.it)[k]||k;}:T;
  var confirmFn=isDebug?"t('nav.confirm_reset')":"T('nav.confirm_reset')";

  var offItems='';
  pages.forEach(function(p){
    var a=(p.key===active)?' active':'';
    offItems+='<a class="fb-nav-item'+a+'" href="'+p.href+'">'+_navSvg(p.key)+'<span data-i18n="'+p.i18n+'">'+p.def+'</span></a>';
  });
  offItems+='<a class="fb-nav-item text-danger" href="#" onclick="if(confirm('+confirmFn+')){fetch(\'/reset\',{method:\'POST\'});}">'+_navSvg('reset')+'<span data-i18n="nav.reset">Reset ESP32</span></a>';

  var desktopItems='';
  pages.forEach(function(p){
    var a=(p.key===active)?' class="active"':'';
    desktopItems+='<a href="'+p.href+'"'+a+'><span data-i18n="'+p.i18n+'">'+p.def+'</span></a>';
  });

  var langSel=isDebug
    ? '<select class="fb-lang" id="lang-sel" onchange="setLang(this.value)"><option value="it">IT</option><option value="en">EN</option></select>'
    : '<select class="fb-lang" id="lang-sel-shared" onchange="setSharedLang(this.value)"><option value="it">IT</option><option value="en">EN</option></select>';

  var html=
    '<div class="offcanvas offcanvas-end fb-offcanvas" tabindex="-1" id="nav-offcanvas">'+
      '<div class="offcanvas-header">'+
        '<h5 class="offcanvas-title">Menu</h5>'+
        '<button type="button" class="btn-close" data-bs-dismiss="offcanvas"></button>'+
      '</div>'+
      '<div class="offcanvas-body p-0">'+offItems+'</div>'+
    '</div>'+
    '<div class="fb-header">'+
      '<div class="fb-logo"><img src="/img/logo.png" alt="FurbyMind" onerror="this.style.display=\'none\'"></div>'+
      '<div class="fb-header-text">'+
        '<img src="/img/title.png" alt="FurbyMind" onerror="this.outerHTML=\'<h1>FurbyMind</h1>\'">'+
        '<p>IP: <span id="esp-ip">—</span> &nbsp;•&nbsp; Uptime: <span id="esp-uptime">—</span></p>'+
      '</div>'+
      '<nav class="fb-nav">'+desktopItems+'<a href="#" class="text-danger" onclick="if(confirm('+confirmFn+')){fetch(\'/reset\',{method:\'POST\'});}"><span data-i18n="nav.reset">Reset ESP32</span></a></nav>'+
      langSel+
      '<button class="fb-hamburger" data-bs-toggle="offcanvas" data-bs-target="#nav-offcanvas"><span></span><span></span><span></span></button>'+
    '</div>';

  document.currentScript
    ? document.currentScript.insertAdjacentHTML('afterend', html)
    : document.body.insertAdjacentHTML('afterbegin', html);
}
// ── i18n ──────────────────────────────────────────────────────────────────────

var _SHARED_TR={
  it:{
    'nav.config':'Configurazione','nav.debug':'Debug','nav.camera':'Camera','nav.reset':'Reset ESP32',
    'nav.confirm_reset':'Riavviare l\'ESP32?',
    'sys.loading':'carico...',
    // index.html
    'wifi.title':'Rete Wi-Fi','wifi.connected':'Connesso: ','wifi.disconnected':'Non connesso',
    'wifi.col_prio':'Priorità','wifi.col_ssid':'SSID','wifi.active':'attiva',
    'wifi.no_nets':'Nessuna rete salvata',
    'wifi.btn_connect':'<i class="bi bi-wifi"></i> Connetti ora','wifi.btn_scan':'<i class="bi bi-search"></i> Scansiona',
    'wifi.label_ssid':'SSID','wifi.label_pass':'Password',
    'wifi.label_prio':'Priorità (0 = massima)',
    'wifi.btn_add':'<i class="bi bi-plus-lg"></i> Aggiungi / Aggiorna rete',
    'wifi.scan_wait':'Scansione in corso (~2s)...','wifi.scan_none':'Nessuna rete trovata.',
    'wifi.scan_err':'Errore scansione.','wifi.scan_use':'<i class="bi bi-arrow-return-right"></i> Usa',
    'wifi.open':'Aperta',
    'wifi.label_hostname':'Hostname',
    'wifi.static_enable':'IP statico',
    'wifi.static_ip':'IP','wifi.static_gw':'Gateway','wifi.static_mask':'Mask','wifi.static_dns':'DNS',
    'wifi.static_required':'IP, gateway e mask sono obbligatori','wifi.static_saved':'Salvato. Riavviare per applicare.',
    'wifi.connect_reboot':'Connettere ora? La board si riavvierà automaticamente per applicare la nuova rete.',
    'sys.save':'Salva',
    'sd.btn_unmount':'<i class="bi bi-eject-fill"></i> Smonta SD','sd.btn_mount':'<i class="bi bi-arrow-clockwise"></i> Monta SD',
    'llm.title':'Configurazione LLM',
    'llm.label_prov':'Fornitore','llm.label_model':'Modello ',
    'llm.label_key':'Token API',
    'llm.btn_save':'<i class="bi bi-floppy"></i> Salva configurazione AI','llm.btn_test':'<i class="bi bi-lightning"></i> Test',
    'llm.test_running':'Test in corso...','llm.test_ok':'✓ OK — ','llm.test_err':'✗ Errore: ','llm.test_timeout':'✗ Timeout/errore',
    'llm.models_loading':'carico...','llm.models_no_key':'token non salvato — lista predefinita',
    'llm.models_none':'nessun modello — lista predefinita','llm.models_offline':'offline — lista predefinita',
    'llm.models_count':' disponibili',
    'el.title':'ElevenLabs TTS',
    'el.label_key':'Token API ElevenLabs','el.label_voice':'Voce',
    'el.label_fmt':'Formato audio',
    'el.btn_save':'<i class="bi bi-floppy"></i> Salva ElevenLabs','el.btn_test':'<i class="bi bi-lightning"></i> Test',
    'el.hint_mp3':'MP3 22kHz 32kbps — funziona con account free, ~4x più leggero in cache',
    'el.hint_pcm':'PCM 16kHz mono — qualità alta, solo account Pro',
    'el.voice_no_key':'Inserisci prima il token API','el.voice_loading':'Carico voci...','el.voice_empty':'Nessuna voce disponibile','el.voice_err':'Errore caricamento voci',
    'sd.title':'Scheda SD','sd.present':'SD presente','sd.absent':'SD assente',
    'sd.absent_sub':'Modalità RAM streaming',
    'sd.btn_reinit':'Rimonta SD',
    'sd.btn_format':'Svuota cache audio',
    'sd.format_confirm':'Cancellare tutta la cache audio SD?',
    'sd.btn_formatfat':'Formatta FAT32',
    'sd.formatfat_confirm':'ATTENZIONE: formattare la SD cancella TUTTI i dati. Continuare?',
    'sd.formatfat_confirm2':'Sei sicuro? Questa operazione è irreversibile.',
    'vad.title':'Microfoni / Rilevamento voce',
    'vad.label_thresh':'Soglia sensibilità (RMS 0–32767, default 800)',
    'vad.btn_en':'<i class="bi bi-mic"></i> Abilita','vad.btn_dis':'<i class="bi bi-mic-mute"></i> Disabilita',
    'vad.active':'VAD attivo','vad.disabled':'VAD disabilitato',
    'stt.desc':'Trascrive il parlato con Whisper prima di passarlo all\'LLM. Richiede chiave OpenAI. +~1-2s di latenza.',
    'stt.btn_en':'Abilita STT','stt.btn_dis':'Disabilita STT',
    'stt.active':'STT (Whisper) attivo','stt.disabled':'STT (Whisper) disabilitato',
    'ble.title':'Bluetooth Furby',
    'ble.connected':'Connesso','ble.connecting':'Connessione...','ble.scanning':'Scansione...',
    'ble.not_connected':'Non connesso',
    'ble.scan':'<i class="bi bi-bluetooth"></i> Cerca Furby','ble.stop':'<i class="bi bi-stop-fill"></i> Stop scan','ble.disconnect':'<i class="bi bi-x-circle"></i> Disconnetti',
    'ble.scanning_msg':'<i class="bi bi-arrow-repeat"></i> Scansione in corso...',
    'ble.found_so_far':'Trovati finora:','ble.furby_found':'Furby trovati:',
    'ble.connect':'<i class="bi bi-link-45deg"></i> Connetti',
    'ble.label_svc':'Service UUID','ble.label_char':'Characteristic UUID TX',
    'ble.btn_save':'<i class="bi bi-floppy"></i> Salva UUID',
    'ble.btn_reset':'<i class="bi bi-arrow-counterclockwise"></i> Ripristina default',
    'ble.connected_for':'connesso da: ',
    'wifi.connect_err':'Errore di rete',
    // camera.html
    'cam.streaming':'Streaming MJPEG attivo','cam.paused':'In pausa',
    'cam.pause':'<i class="bi bi-pause-fill"></i> Pausa','cam.resume':'<i class="bi bi-play-fill"></i> Riprendi',
    'cam.quality_high':'Qualità alta','cam.quality_low':'Qualità bassa',
    'cam.describe_title':'Descrivi frame con LLM',
    'cam.describe_hint':'Cattura un frame e chiedi all\'LLM configurato di descriverlo con il prompt qui sotto.',
    'cam.save_prompt':'<i class="bi bi-floppy"></i> Salva prompt','cam.describe_now':'<i class="bi bi-eye"></i> Descrivi ora',
    'cam.tts_voice':'Output vocale',
    'cam.tts_browser':'Browser','cam.tts_board':'Board',
    'cam.tts_hint':'Browser: Web Speech API — Board: ElevenLabs via ESP32',
    'cam.saved':'Salvato.','cam.error':'Errore.',
    'cam.sending':'Analisi in corso...','cam.error_conn':'Errore connessione.',
    'cam.tts_sending':'Invio a ElevenLabs...','cam.tts_playing':'In riproduzione sulla board.',
    'cam.tts_err':'Errore TTS: ','cam.speech_unsupported':'Web Speech API non supportata in questo browser.',
    'cam.reset_confirm':'Riavviare l\'ESP32?',
    // wifi_connect.html
    'wc.connecting':'Connessione in corso…','wc.connected':'Connesso!','wc.failed':'Connessione fallita',
    'wc.back':'<i class="bi bi-house"></i> Torna alla home',
    'fw.update_avail':'Aggiornamento disponibile: ','fw.current':'corrente: ',
    'fw.up_to_date':'Firmware aggiornato ','fw.local_newer':'Versione locale (','fw.vs_online':') più recente di quella online (',
    'sys.flash_used':'usati','sys.flash_free':'liberi',
    'wifi.placeholder_ssid':'Nome rete','wifi.placeholder_pass':'Password (lascia vuoto se aperta)',
    'llm.placeholder_key':'Token API...','el.placeholder_key':'API key...',
    'sd.cache_cleared':'Cache eliminata.','sd.formatting':'Formattazione in corso...','sd.format_done':'Formattazione completata.',
    'error_generic':'Errore: '
  },
  en:{
    'nav.config':'Configuration','nav.debug':'Debug','nav.camera':'Camera','nav.reset':'Reset ESP32',
    'nav.confirm_reset':'Restart the ESP32?',
    'sys.loading':'loading...',
    // index.html
    'wifi.title':'Wi-Fi Network','wifi.connected':'Connected: ','wifi.disconnected':'Not connected',
    'wifi.col_prio':'Priority','wifi.col_ssid':'SSID','wifi.active':'active',
    'wifi.no_nets':'No saved networks',
    'wifi.btn_connect':'<i class="bi bi-wifi"></i> Connect now','wifi.btn_scan':'<i class="bi bi-search"></i> Scan',
    'wifi.label_ssid':'SSID','wifi.label_pass':'Password',
    'wifi.label_prio':'Priority (0 = highest)',
    'wifi.btn_add':'<i class="bi bi-plus-lg"></i> Add / Update network',
    'wifi.scan_wait':'Scanning (~2s)...','wifi.scan_none':'No networks found.',
    'wifi.scan_err':'Scan error.','wifi.scan_use':'<i class="bi bi-arrow-return-right"></i> Use',
    'wifi.open':'Open',
    'wifi.label_hostname':'Hostname',
    'wifi.static_enable':'Static IP',
    'wifi.static_ip':'IP','wifi.static_gw':'Gateway','wifi.static_mask':'Mask','wifi.static_dns':'DNS',
    'wifi.static_required':'IP, gateway and mask are required','wifi.static_saved':'Saved. Reboot to apply.',
    'wifi.connect_reboot':'Connect now? The board will reboot automatically to apply the new network.',
    'sys.save':'Save',
    'sd.btn_unmount':'<i class="bi bi-eject-fill"></i> Unmount SD','sd.btn_mount':'<i class="bi bi-arrow-clockwise"></i> Mount SD',
    'llm.title':'LLM Configuration',
    'llm.label_prov':'Provider','llm.label_model':'Model ',
    'llm.label_key':'API Token',
    'llm.btn_save':'<i class="bi bi-floppy"></i> Save AI configuration','llm.btn_test':'<i class="bi bi-lightning"></i> Test',
    'llm.test_running':'Testing...','llm.test_ok':'✓ OK — ','llm.test_err':'✗ Error: ','llm.test_timeout':'✗ Timeout/error',
    'llm.models_loading':'loading...','llm.models_no_key':'token not saved — default list',
    'llm.models_none':'no models — default list','llm.models_offline':'offline — default list',
    'llm.models_count':' available',
    'el.title':'ElevenLabs TTS',
    'el.label_key':'ElevenLabs API Token','el.label_voice':'Voice',
    'el.label_fmt':'Audio format',
    'el.btn_save':'<i class="bi bi-floppy"></i> Save ElevenLabs','el.btn_test':'<i class="bi bi-lightning"></i> Test',
    'el.hint_mp3':'MP3 22kHz 32kbps — works with free account, ~4x lighter in cache',
    'el.hint_pcm':'PCM 16kHz mono — high quality, Pro account only',
    'el.voice_no_key':'Enter API token first','el.voice_loading':'Loading voices...','el.voice_empty':'No voices available','el.voice_err':'Error loading voices',
    'sd.title':'SD Card','sd.present':'SD present','sd.absent':'SD absent',
    'sd.absent_sub':'RAM streaming mode',
    'sd.btn_reinit':'Remount SD',
    'sd.btn_format':'Clear audio cache',
    'sd.format_confirm':'Delete all SD audio cache?',
    'sd.btn_formatfat':'Format FAT32',
    'sd.formatfat_confirm':'WARNING: formatting the SD will erase ALL data. Continue?',
    'sd.formatfat_confirm2':'Are you sure? This operation is irreversible.',
    'vad.title':'Microphones / Voice detection',
    'vad.label_thresh':'Sensitivity threshold (RMS 0–32767, default 800)',
    'vad.btn_en':'<i class="bi bi-mic"></i> Enable','vad.btn_dis':'<i class="bi bi-mic-mute"></i> Disable',
    'vad.active':'VAD active','vad.disabled':'VAD disabled',
    'stt.desc':'Transcribes speech with Whisper before passing it to the LLM. Requires OpenAI key. +~1-2s latency.',
    'stt.btn_en':'Enable STT','stt.btn_dis':'Disable STT',
    'stt.active':'STT (Whisper) active','stt.disabled':'STT (Whisper) disabled',
    'ble.title':'Bluetooth Furby',
    'ble.connected':'Connected','ble.connecting':'Connecting...','ble.scanning':'Scanning...',
    'ble.not_connected':'Not connected',
    'ble.scan':'<i class="bi bi-bluetooth"></i> Find Furby','ble.stop':'<i class="bi bi-stop-fill"></i> Stop scan','ble.disconnect':'<i class="bi bi-x-circle"></i> Disconnect',
    'ble.scanning_msg':'<i class="bi bi-arrow-repeat"></i> Scanning...',
    'ble.found_so_far':'Found so far:','ble.furby_found':'Furby found:',
    'ble.connect':'<i class="bi bi-link-45deg"></i> Connect',
    'ble.label_svc':'Service UUID','ble.label_char':'Characteristic UUID TX',
    'ble.btn_save':'<i class="bi bi-floppy"></i> Save UUID',
    'ble.btn_reset':'<i class="bi bi-arrow-counterclockwise"></i> Restore defaults',
    'ble.connected_for':'connected for: ',
    'wifi.connect_err':'Network error',
    // camera.html
    'cam.streaming':'MJPEG stream active','cam.paused':'Paused',
    'cam.pause':'<i class="bi bi-pause-fill"></i> Pause','cam.resume':'<i class="bi bi-play-fill"></i> Resume',
    'cam.quality_high':'High quality','cam.quality_low':'Low quality',
    'cam.describe_title':'Describe frame with LLM',
    'cam.describe_hint':'Capture a frame and ask the configured LLM to describe it using the prompt below.',
    'cam.save_prompt':'<i class="bi bi-floppy"></i> Save prompt','cam.describe_now':'<i class="bi bi-eye"></i> Describe now',
    'cam.tts_voice':'Voice output',
    'cam.tts_browser':'Browser','cam.tts_board':'Board',
    'cam.tts_hint':'Browser: Web Speech API — Board: ElevenLabs via ESP32',
    'cam.saved':'Saved.','cam.error':'Error.',
    'cam.sending':'Analysing...','cam.error_conn':'Connection error.',
    'cam.tts_sending':'Sending to ElevenLabs...','cam.tts_playing':'Playing on board.',
    'cam.tts_err':'TTS error: ','cam.speech_unsupported':'Web Speech API not supported in this browser.',
    'cam.reset_confirm':'Restart the ESP32?',
    // wifi_connect.html
    'wc.connecting':'Connecting…','wc.connected':'Connected!','wc.failed':'Connection failed',
    'wc.back':'<i class="bi bi-house"></i> Back to home',
    'fw.update_avail':'Update available: ','fw.current':'current: ',
    'fw.up_to_date':'Firmware up to date ','fw.local_newer':'Local version (','fw.vs_online':') newer than online (',
    'sys.flash_used':'used','sys.flash_free':'free',
    'wifi.placeholder_ssid':'Network name','wifi.placeholder_pass':'Password (leave blank if open)',
    'llm.placeholder_key':'API token...','el.placeholder_key':'API key...',
    'sd.cache_cleared':'Cache cleared.','sd.formatting':'Formatting...','sd.format_done':'Format complete.',
    'error_generic':'Error: '
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
    else el.innerHTML=T(k);
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
var _fwLatestVersion = null;
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
  if(!_uptimeTimer)_uptimeTimer=setInterval(_uptimeTick,1000);
  _uptimeTick();
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

function _fwCompare(a,b){
  var pa=String(a).split('.').map(Number),pb=String(b).split('.').map(Number);
  for(var i=0;i<Math.max(pa.length,pb.length);i++){
    var da=(pa[i]||0)-(pb[i]||0);if(da!==0)return da>0?1:-1;
  }
  return 0;
}

function _renderSysBar(bar, plainText, tempC, fwLatest){
  while(bar.firstChild) bar.removeChild(bar.firstChild);
  bar.appendChild(document.createTextNode(plainText));
  // temperatura + badge fw come ultimo blocco combinato
  if(tempC!=null||fwLatest){
    bar.appendChild(document.createTextNode(' | '));
    var wrap=document.createElement('span');
    wrap.className='d-inline-flex align-items-center gap-1';
    if(tempC!=null){
      var col=_tempColor(tempC);
      var tsp=document.createElement('span');
      if(col) tsp.className='fw-bold';
      if(col) tsp.style.color=col;
      tsp.textContent='T '+tempC+'°C';
      wrap.appendChild(tsp);
    }
    if(fwLatest&&_fwCurrent){
      var cmp=_fwCompare(_fwCurrent,fwLatest);
      var badge=document.createElement('span');
      badge.setAttribute('data-bs-toggle','tooltip');
      badge.setAttribute('data-bs-placement','bottom');
      if(cmp<0){
        badge.className='badge bg-danger ms-1';
        badge.title=T('fw.update_avail')+fwLatest+' ('+T('fw.current')+_fwCurrent+')';
        badge.innerHTML='<i class="bi bi-exclamation-circle-fill"></i> '+_fwCurrent;
      } else if(cmp===0){
        badge.className='badge bg-success ms-1';
        badge.title=T('fw.up_to_date')+'('+_fwCurrent+')';
        badge.innerHTML='<i class="bi bi-check-circle-fill"></i> '+_fwCurrent;
      } else {
        badge.className='badge bg-warning text-dark ms-1';
        badge.title=T('fw.local_newer')+_fwCurrent+T('fw.vs_online')+fwLatest+')';
        badge.innerHTML='<i class="bi bi-question-circle-fill"></i> '+_fwCurrent;
      }
      wrap.appendChild(badge);
      if(window.bootstrap&&window.bootstrap.Tooltip)new window.bootstrap.Tooltip(badge);
    }
    bar.appendChild(wrap);
  }
}

function _updateSysBar(d){
  var sdPct=d.sd_total_mb?Math.round(d.sd_used_mb*100/d.sd_total_mb):0;
  var sd=d.sd_total_mb?' | SD '+d.sd_used_mb+'/'+d.sd_total_mb+' MB ('+sdPct+'%)':'';
  var flashUsed=d.flash_used_kb!=null?fmtKB(d.flash_used_kb):'?';
  var flashFree=d.flash_free_kb!=null?fmtKB(d.flash_free_kb):'?';
  var bat=d.bat_mv>0?' | BAT '+(d.bat_mv/1000).toFixed(2)+'V':'';
  var bar=document.getElementById('sys-bar')||document.getElementById('sys-info-bar');
  var sramUsed=d.heap_total-d.heap_free;
  var sramPct=d.heap_total?Math.round(sramUsed*100/d.heap_total):0;
  var psramUsed=d.psram_total-d.psram_free;
  var psramPct=d.psram_total?Math.round(psramUsed*100/d.psram_total):0;
  _lastSysPlain='CPU '+d.cpu_mhz+'MHz'
    +' | SRAM '+fmtKB(sramUsed)+'/'+fmtKB(d.heap_total)+' ('+sramPct+'%)'
    +' | PSRAM '+fmtKB(psramUsed)+'/'+fmtKB(d.psram_total)+' ('+psramPct+'%)'
    +' | Sketch '+fmtKB(d.sketch_used)+'/'+fmtKB(d.sketch_total)
    +' | SPIFFS '+fmtKB(d.spiffs_used)+'/'+fmtKB(d.spiffs_total)
    +' | Flash '+flashUsed+' '+T('sys.flash_used')+', '+flashFree+' '+T('sys.flash_free')+' ('+d.flash_mb+'MB)'+bat+sd;
  _lastTempC=(d.chip_temp_c!=null)?d.chip_temp_c:null;
  if(bar) _renderSysBar(bar, _lastSysPlain, _lastTempC, _fwLatestVersion);
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
        if(latest){
          _fwLatestVersion=latest;
          _fwUpdateAvailable=_fwCompare(_fwCurrent,latest)<0;
          var bar=document.getElementById('sys-bar')||document.getElementById('sys-info-bar');
          if(bar) _renderSysBar(bar, _lastSysPlain, _lastTempC, latest);
        }
      }).catch(function(){});
  }
}

function _loaderDone(){
  var l=document.getElementById('fb-loader');
  if(!l)return;
  l.classList.add('fb-loader-done');
  setTimeout(function(){if(l.parentNode)l.parentNode.removeChild(l);},300);
}

document.addEventListener('DOMContentLoaded',function(){
  var ipEl=document.getElementById('esp-ip');
  if(ipEl)ipEl.textContent=window.location.hostname;
  if(_bootRef){_uptimeTimer=setInterval(_uptimeTick,1000);_uptimeTick();}

  applyI18n();
  _loaderDone();

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
