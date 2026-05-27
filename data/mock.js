// mock.js - intercettore fetch/EventSource per debug frontend senza ESP32
// Attivazione: aggiungi ?mock=1 all'URL. Persiste in sessionStorage finchè la tab è aperta.
// Per disattivare: ?mock=0 oppure chiudi la tab.

(function () {
  // ── SAFETY: il mock NON deve mai attivarsi quando la pagina è servita dall'ESP32 reale.
  // Whitelist host ammessi: localhost, 127.x, file://, IP non-routable di sviluppo.
  // Tutto il resto (furby.local, 192.168.*, IP LAN tipici) è considerato device reale.
  var host = location.hostname || '';
  var proto = location.protocol || '';
  var isDevHost = (proto === 'file:')
                || host === 'localhost'
                || host === '127.0.0.1'
                || host === '0.0.0.0'
                || host === '::1'
                || host === '';
  if (!isDevHost) {
    console.warn('[MOCK] disattivato: host="' + host + '" non è un host di sviluppo. Il mock funziona solo su localhost/file://.');
    localStorage.removeItem('furby_mock'); // ripulisci eventuali residui
    return;
  }
  var p = new URLSearchParams(location.search);
  if (p.has('mock')) {
    if (p.get('mock') === '0') { localStorage.removeItem('furby_mock'); }
    else                        { localStorage.setItem('furby_mock', '1'); }
  }
  if (localStorage.getItem('furby_mock') !== '1') return;

  // ── stato in-memory che simula la flash del device ──────────────────────────
  // Tutto in localStorage. Per resettare: localStorage.removeItem('furby_mock_state').
  var ST = JSON.parse(localStorage.getItem('furby_mock_state') || 'null') || {
    personalities: {
      active: 0,
      list: [{
        id: 'default', name: 'Default', lang: 'it',
        voice_id: 'XnnY3M0feCKwyNnn9JRF',
        prompt: 'Sei un Furby cinico e maleducato. Rispondi BREVE (max 12 parole).',
        behaviors: [{
          id: 'b1', name: 'Antenna SX', trigger: 2, sensor_id: 1,
          consequences: [{ type: 5, snapshot: false, ctx_beh_name: false, ctx_sensor: false }]
        },{
          id: 'b2', name: 'Bottone (canned random)', trigger: 1, sensor_id: 0,
          consequences: [{ type: 6, canned_random: true, canned_id: '' }]
        }],
        canned: [
          { id:'k_it_1', text:'cazzo',     file:'it/1.mp3' },
          { id:'k_it_2', text:'fanculo',   file:'' },
          { id:'k_it_3', text:'porcoddio', file:'' }
        ]
      },{
        id: 'mock_en', name: 'Mock EN', lang: 'en',
        voice_id: 'XnnY3M0feCKwyNnn9JRF',
        prompt: 'You are a cynical, rude Furby. Reply SHORT (max 12 words).',
        behaviors: [],
        canned: [
          { id:'k_en_1', text:'fuck',     file:'' },
          { id:'k_en_2', text:'fuck off', file:'' },
          { id:'k_en_3', text:'damn',     file:'' }
        ]
      }]
    },
    ble:    { connected: false, scanning: false, connecting: false, name: '', devices: [] },
    camera: { framesize: 7, quality: 12, hmirror: 0, vflip: 0, flicker: 0 },
    home:   { ssid: 'MockNet', ip: '192.168.4.42', rssi: -52, uptime: 12345,
              sd: { present: true, type: 'SDHC', total_mb: 32000, used_mb: 120 },
              ble: { connected: false, name: '' },
              battery: { mv: 4012, pct: 87, usb: true, charging: false } },
    vad:    { threshold: 800, mic_gain: 10, vad_enabled: true },
    llm:    { provider: 'openai', model: 'gpt-4o-mini', openai_key: '****', claude_key: '' },
    el:     { voice_id: 'XnnY3M0feCKwyNnn9JRF', api_key: '****', fmt: 'mp3' },
    cprompt:'Descrivi cosa vedi in una frase cinica.',
    sysinfo:{ hostname:'furby', ip:'192.168.4.42', ssid:'MockNet', rssi:-52,
              uptime_s:12345, heap_free:120000, heap_total:300000,
              psram_free:7000000, psram_total:8000000,
              fw_version: location.pathname.endsWith('/info.html')?'mock':'',
              ntp_time:'2026-05-27 12:34:56',
              battery:{mv:4012,pct:87,usb:true,charging:false},
              sd:{present:true,type:'SDHC',total_mb:32000,used_mb:120} }
  };
  function persist(){ localStorage.setItem('furby_mock_state', JSON.stringify(ST)); }

  var SENSORS = [
    { id: 1, name: 'Antenna SX' }, { id: 2, name: 'Antenna DX' },
    { id: 3, name: 'Pancia' },     { id: 4, name: 'Coda' },
    { id: 5, name: 'Lingua' },     { id: 6, name: 'Inclina avanti' }
  ];

  var ACTIONS = [
    { id: 'laugh',   label: 'Ridi',            label_en: 'Laugh' },
    { id: 'burp',    label: 'Rutta',           label_en: 'Burp' },
    { id: 'fart',    label: 'Scoreggia',       label_en: 'Fart' },
    { id: 'sneeze',  label: 'Starnutisci',     label_en: 'Sneeze' },
    { id: 'sing',    label: 'Canta',           label_en: 'Sing' },
    { id: 'sleep',   label: 'Dormi',           label_en: 'Sleep' }
  ];

  // ── helpers ───────────────────────────────────────────────────────────────
  function jres(obj, status){ status = status || 200;
    return new Response(JSON.stringify(obj), { status: status, headers: { 'Content-Type':'application/json' } });
  }
  function tres(txt, status){ status = status || 200;
    return new Response(txt, { status: status, headers: { 'Content-Type':'text/plain' } });
  }
  function parseBody(init){
    if (!init || !init.body) return {};
    var b = init.body;
    if (b instanceof URLSearchParams) {
      var o = {}; b.forEach(function(v,k){ o[k]=v; }); return o;
    }
    if (b instanceof FormData) {
      var o = {}; b.forEach(function(v,k){ o[k]=v; }); return o;
    }
    if (typeof b === 'string') {
      var o = {};
      try { var sp = new URLSearchParams(b); sp.forEach(function(v,k){ o[k]=v; }); return o; }
      catch(e){ try { return JSON.parse(b); } catch(_){ return { raw: b }; } }
    }
    return {};
  }

  // ── routes (sync; ritornano Response oppure null se nessun match) ──────────
  function route(method, url, init) {
    var u = url.split('?')[0];
    var q = url.indexOf('?') >= 0 ? Object.fromEntries(new URLSearchParams(url.split('?')[1])) : {};
    var body = parseBody(init);

    if (method === 'GET') {
      if (u === '/personalities')           {
        var p = ST.personalities;
        return jres({ active: p.active, origin: 'spiffs', personalities: p.list });
      }
      if (u === '/sensors/list')            return jres(SENSORS);
      if (u === '/behaviors/actions')       return jres({ actions: ACTIONS });
      if (u === '/api/home')                return jres(ST.home);
      if (u === '/sys/info')                return jres(ST.sysinfo);
      if (u === '/ble/status')              return jres(ST.ble);
      if (u === '/camera/settings')         return jres(ST.camera);
      if (u === '/camera/describe/prompt')  return jres({ prompt: ST.cprompt });
      if (u === '/api/voices')              return jres([
        { voice_id: 'XnnY3M0feCKwyNnn9JRF', name: 'Mock Voice 1' },
        { voice_id: 'aaaaaaaaaaaaaaaaaaa1', name: 'Mock Voice 2' }
      ]);
      if (u === '/api/models')              return jres({ models: ['gpt-4o-mini', 'gpt-4o', 'claude-sonnet-4-6'] });
      if (u === '/api/wifi/scan')           return jres({ nets: [
        { ssid:'MockNet',       rssi:-42, enc:'WPA2' },
        { ssid:'Casa39',        rssi:-58, enc:'WPA2' },
        { ssid:'OpenGuest',     rssi:-70, enc:'OPEN' }
      ] });
      if (u === '/version.txt')             return tres('mock-' + new Date().toISOString().slice(0,16).replace(/[-:T]/g,''));
      if (u === '/api/test')                return jres({ ok: true, info: 'mock ok ('+q.type+')' });
      if (u === '/fs/list')                 return jres({ used: 120000, total: 1500000, files: [
        { name: '/personalities.json', size: 1024 },
        { name: '/index.html',         size: 4096 }
      ] });
      if (u === '/sd/list') {
        var dir = q.dir || '/';
        if (dir === '/') return jres({ used: 120000000, total: 32000000000, dir: dir, files: [
          { name: 'default', size: 0, dir: true },
          { name: 'index.json', size: 256, dir: false }
        ] });
        return jres({ used: 120000000, total: 32000000000, dir: dir, files: [
          { name: 'audio_1.mp3', size: 12000, dir: false }
        ] });
      }
      if (u === '/wifi/connect/status')     return jres({ status:'connected', ssid:ST.home.ssid, ip:ST.home.ip });
      if (u.indexOf('/fs/get') === 0)       return tres('// mock file content for ' + u);
      if (u === '/sd/get')                  return tres('mock audio bytes');
    }

    if (method === 'POST') {
      if (u === '/personalities/activate')  { ST.personalities.active = parseInt(body.idx || 0); persist(); return jres({ ok:true }); }
      if (u === '/personalities/new')       {
        var nm = body.name || 'Nuova';
        ST.personalities.list.push({ id:'p'+Date.now(), name:nm, lang:'it', voice_id:'', prompt:'', behaviors:[], canned:[] });
        persist(); return jres({ ok:true });
      }
      if (u === '/personalities/del')       { ST.personalities.list.splice(parseInt(body.idx||0),1); persist(); return jres({ ok:true }); }
      if (u === '/personalities/save')      {
        try {
          var p = JSON.parse(init.body);
          // applica la validazione che fa il backend
          for (var i=0;i<(p.personalities||[]).length;i++){
            var per=p.personalities[i];
            for (var j=0;j<(per.behaviors||[]).length;j++){
              var b=per.behaviors[j];
              if (b.trigger==null||b.trigger<0||b.trigger>2) return jres({ ok:false, error:'trigger mancante o non valido' }, 400);
              if (!b.consequences||!b.consequences.length)   return jres({ ok:false, error:'comportamento senza conseguenze' }, 400);
            }
          }
          ST.personalities = p; persist();
        } catch(e){ return jres({ ok:false, error:'JSON non valido' }, 400); }
        return jres({ ok:true });
      }
      if (u === '/test/simulate') {
        var pi = parseInt(body.personality||0);
        var per = ST.personalities.list[pi] || {};
        var beh = (per.behaviors||[])[0] || null;
        var canned = per.canned || [];
        var log = '[MOCK SIM] trigger='+body.trigger+' sensor='+body.sensor_id+'\n'
                + '[MOCK SIM] skip_llm='+body.skip_llm+' skip_tts='+body.skip_tts+' skip_ble='+body.skip_ble+'\n'
                + '[MOCK SIM] audio_local='+body.audio_local+'\n';
        // analizza le conseguenze del primo behavior per simulare CSQ_CANNED (type=6)
        if (beh && beh.consequences) {
          beh.consequences.forEach(function(c, i){
            if (c.type === 6) {
              var pick;
              if (c.canned_random || !c.canned_id) pick = canned[Math.floor(Math.random()*canned.length)];
              else pick = canned.find(function(k){ return k.id === c.canned_id; });
              if (pick) {
                if (body.skip_tts === '1') {
                  log += '[MOCKED]\nplay: ' + (pick.file || '(da generare)') + ' (' + pick.text + ')\n';
                } else {
                  log += '[CSQ] CANNED play: ' + (pick.file || '(generato ora)') + ' (' + pick.text + ')\n';
                }
              } else {
                log += '[CSQ] CANNED: nessuna frase pronta disponibile\n';
              }
            } else {
              log += '[CSQ '+(i+1)+'] tipo='+c.type+'\n';
            }
          });
        }
        log += '[LLM] risposta finale: "(mock) ecco una frase finta cinica"\n';
        log += '[SPEAK] "(mock) ecco una frase finta cinica" (SD=si)\n';
        var out = { ok:true, log: log };
        if (body.audio_local === '1') {
          out.audio_path   = '/default/it/mock.mp3';
          out.audio_source = 'spiffs';
        }
        return jres(out);
      }
      if (u === '/test/tts')                return jres({ ok:true });
      if (u === '/camera/describe')         return jres({ ok:true, text:'(mock) vedo uno sviluppatore davanti a uno schermo' });
      if (u === '/camera/settings')         { Object.assign(ST.camera, body); persist(); return jres({ ok:true }); }
      if (u === '/camera/describe/prompt')  { ST.cprompt = body.prompt || ''; persist(); return jres({ ok:true }); }
      if (u === '/vad/save')                { Object.assign(ST.vad, body); persist(); return jres({ ok:true }); }
      if (u === '/llm/save')                { Object.assign(ST.llm, body); persist(); return jres({ ok:true }); }
      if (u === '/el/save')                 { Object.assign(ST.el, body);  persist(); return jres({ ok:true }); }
      if (u === '/ble/scan')                { ST.ble = { connected:false, scanning:true, connecting:false, name:'', devices:[
                                                { addr:'AA:BB:CC:DD:EE:01', name:'Furby Mock A', rssi:-60 },
                                                { addr:'AA:BB:CC:DD:EE:02', name:'Furby Mock B', rssi:-72 }
                                              ] }; persist(); return jres({ ok:true }); }
      if (u === '/ble/scan/stop')           { ST.ble.scanning = false; persist(); return jres({ ok:true }); }
      if (u === '/ble/connect')             { ST.ble = { connected:true, scanning:false, connecting:false, name:'Furby Mock', devices:[] }; persist(); return jres({ ok:true }); }
      if (u === '/ble/disconnect')          { ST.ble.connected = false; ST.ble.name=''; persist(); return jres({ ok:true }); }
      if (u === '/ble/abort')               { ST.ble.scanning=false; ST.ble.connecting=false; persist(); return jres({ ok:true }); }
      if (u === '/ble/reset')               { ST.ble={ connected:false, scanning:false, connecting:false, name:'', devices:[] }; persist(); return jres({ ok:true }); }
      if (u === '/debug/cmd')               return jres({ ok:true, response: '00112233' });
      if (u === '/debug/vol')               return jres({ ok:true });
      if (u === '/debug/tone')              return jres({ ok:true, freq: parseInt(body.freq||1000), ms: parseInt(body.ms||500) });
      if (u === '/debug/mic/record')        return jres({ ok:true, samples: 80000, rms: 1234 });
      if (u === '/wifi/connect')            return jres({ ok:true });
      if (u === '/wifi/hostname')           return jres({ ok:true });
      if (u === '/wifi/static')             return jres({ ok:true });
      if (u === '/sd/reinit')               return jres({ ok:true, type:'SDHC', total_mb:32000, used_mb:120 });
      if (u === '/sd/unmount')              return jres({ ok:true });
      if (u === '/sd/format')               return jres({ ok:true });
      if (u === '/sd/formatfat')            return jres({ ok:true });
      if (u === '/fs/put' || u === '/sd/put' || u === '/fs/del' || u === '/sd/del') return jres({ ok:true });
      if (u === '/reset')                   return jres({ ok:true });
    }

    return null; // nessun match
  }

  // ── intercept fetch ────────────────────────────────────────────────────────
  var _origFetch = window.fetch.bind(window);
  window.fetch = function (input, init) {
    var url    = (typeof input === 'string') ? input : input.url;
    var method = (init && init.method) || (typeof input !== 'string' && input.method) || 'GET';
    method     = method.toUpperCase();

    // lascia passare richieste esterne (https://...) e asset statici
    if (/^https?:\/\//i.test(url)) return _origFetch(input, init);
    if (/\.(html|css|js|png|jpg|woff2|ico|svg)$/i.test(url.split('?')[0])) return _origFetch(input, init);

    var resp = route(method, url, init);
    if (resp) {
      console.debug('[MOCK]', method, url, '→', resp.status);
      return Promise.resolve(resp);
    }
    console.warn('[MOCK] non gestito:', method, url, '— passo al fetch reale (probabilmente fallirà)');
    return _origFetch(input, init);
  };

  // ── intercept EventSource (mic SSE, BLE SSE, sensors SSE) ─────────────────
  var _OrigES = window.EventSource;
  window.EventSource = function (url) {
    if (/^\/(debug\/mic|debug\/sensors|ble\/status)\/sse/.test(url)) {
      console.debug('[MOCK] SSE', url);
      var fake = { close: function(){ if (this._t) clearInterval(this._t); }, onmessage: null, onerror: null };
      var tick = 0;
      fake._t = setInterval(function () {
        if (!fake.onmessage) return;
        var msg = null;
        if (url.indexOf('/debug/mic/sse') === 0) {
          var samples = []; for (var i=0;i<32;i++) samples.push(Math.round(Math.sin(tick*0.3 + i*0.4)*8000 + (Math.random()*2000-1000)));
          msg = { rms: 800 + Math.round(Math.random()*500), samples: samples };
        } else if (url.indexOf('/ble/status/sse') === 0) {
          msg = ST.ble;
        } else if (url.indexOf('/debug/sensors/sse') === 0) {
          msg = { sensors: [0,0,Math.random()>0.9?1:0,0,0,0] };
        }
        if (msg) fake.onmessage({ data: JSON.stringify(msg) });
        tick++;
      }, 200);
      return fake;
    }
    return new _OrigES(url);
  };

  // ── banner visivo ─────────────────────────────────────────────────────────
  document.addEventListener('DOMContentLoaded', function () {
    var bar = document.createElement('div');
    bar.textContent = 'MOCK MODE — nessun ESP32 collegato (chiudi tab per uscire o aggiungi ?mock=0)';
    bar.style.cssText = 'position:fixed;left:0;right:0;bottom:0;z-index:9999;background:#ffc107;color:#000;padding:6px 10px;font:600 12px system-ui;text-align:center;border-top:2px solid #b58900';
    document.body.appendChild(bar);
  });
})();
