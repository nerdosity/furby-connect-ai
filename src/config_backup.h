#pragma once

// Salva tutte le variabili NVS come JSON su SPIFFS e SD (se presente).
// Chiamare dopo ogni nvsPut() e dopo ogni salvataggio WiFi.
void saveConfigBackup();

// Se NVS era vuoto al boot (nvsWasEmpty=true), tenta di ripristinare
// da SPIFFS /config_bak.json; se corrotto, prova da SD.
// Ritorna true se ha ripristinato qualcosa.
bool loadConfigBackupIfNeeded(bool nvsWasEmpty);
