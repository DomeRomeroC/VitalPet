#include <Arduino.h>
#include "secrets.h"
#include "apwifieeprommode.h"
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ThingSpeak.h>
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>
#include <time.h>
#include <ESPmDNS.h>

// --- LIBRERÍAS DE SERVIDOR WEB Y SISTEMA DE ARCHIVOS ---
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// --- LIBRERÍAS DE AUDIO ---
#include "AudioGeneratorMP3.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioFileSourceLittleFS.h"
#include "AudioOutputI2S.h"
#include "audio_memoria.h"

// ====================================================================
// 1. ESTRUCTURAS DE MEMORIA (EEPROM), TELEGRAM Y THINGSPEAK
// ====================================================================
const String BOT_TOKEN = TELEGRAM_BOT_TOKEN;
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
WiFiClient clientThingSpeak;

const unsigned long CHANNEL_ID = THINGSPEAK_CHANNEL_ID;
const char* WRITE_API_KEY = THINGSPEAK_WRITE_API_KEY;

struct PerfilMascota {
    char especie;
    char sexo;
    float peso;
    float v_req;
    int configurado;
    int hora_alerta;
    int minuto_alerta;
    float factor_escala;
    int32_t tare_offset; 
    uint32_t magic;      
} perfil;

int pasoConfigTelegram = 0;
bool reporteEnviadoHoy = false;

volatile bool solicitarTara = false;
volatile bool solicitarCalibracion = false;
volatile float volumenCalibracion = 0.0;
volatile bool modoCambioAgua = false;       
volatile bool solicitarFinCambio = false;   

float consumoHoy = 0.0;           
float ultimoPesoEstable = -1.0;   
bool gatoEstabaPresente = false;  
float pesoAntesDelGato = -1.0;    

void guardarPerfilEEPROM() {
    EEPROM.put(350, perfil);
    EEPROM.commit();
}

void cargarPerfilEEPROM() {
    EEPROM.get(350, perfil);
    
    if (perfil.magic != 0xABCD5678) {
        Serial.println("ℹ️ Inicializando EEPROM con nuevos valores de calibración...");
        if (isnan(perfil.v_req) || perfil.configurado < 0 || perfil.configurado > 1) {
            perfil.configurado = 0;
            perfil.hora_alerta = 20;
            perfil.minuto_alerta = 0;
        }
        perfil.factor_escala = -287.7233; 
        perfil.tare_offset = -12977;       
        perfil.magic = 0xABCD5678;
        guardarPerfilEEPROM();
    }
    
    if(isnan(perfil.factor_escala) || perfil.factor_escala == 0.0) {
        perfil.factor_escala = -287.7233;
    }
}

// ====================================================================
// 2. HARDWARE, SENSORES Y GLOBALES
// ====================================================================
HX711 balanza;
const int PIN_TDS  = 34; 
const int PIN_TEMP = 27;
const int PIN_I2S_LRC = 25, PIN_I2S_BCLK = 26, PIN_I2S_DIN = 33;
const int PIN_DOUT = 17, PIN_SCK = 16;
const int PIN_TRIG = 18, PIN_ECHO = 19;
const int PIN_BOMBA = 23;

OneWire oneWireTemp(PIN_TEMP);
DallasTemperature sensorTemp(&oneWireTemp);
bool sensorTempDetectado = false;

AsyncWebServer servidorWebAudio(8080);
AudioGeneratorMP3 *mp3 = NULL;
AudioFileSourcePROGMEM *fileProgmem = NULL;
AudioFileSourceLittleFS *fileLittleFS = NULL;
AudioOutputI2S *out = NULL;

const char* ARCHIVO_VOZ = "/voz_usuario.mp3";
bool reproducirAudioNuevo = false;
bool solicitarReproduccion = false;

const float VREF = 3.3;
const int RESOLUCION_ADC = 4096;
const float TDS_MAXIMO = 300.0;

float pesoGlobal = 0.0, tempGlobal = 25.0, tdsGlobal = 0.0, distanciaGlobal = 999.0;
bool gatoDetectadoEsteCiclo = false, bombaEncendida = false;
int contadorFiltroGato = 0;
unsigned long tiempoUltimoGato = 0;

String id_del_chat = "";

SemaphoreHandle_t mutexRed;
const TickType_t TIMEOUT_MUTEX = pdMS_TO_TICKS(2500);

unsigned long ultimaLecturaTemp = 0;
unsigned long ultimaLecturaTDS = 0;
unsigned long ultimoEnvioTS = 0;
const unsigned long tiempoEsperaTS = 600000;

unsigned long ultimaAlerta = 0;
const unsigned long tiempoEsperaAlerta = 300000;
unsigned long ultimaAlertaTDS = 0;
const unsigned long tiempoEsperaAlertaTDS = 300000;

bool tanqueEstabaBajo = false;
bool tdsEstabaMalo = false;

// ====================================================================
// 3. INTERFACES WEB (MP3 Y DASHBOARD CLINICO DE 24H)
// ====================================================================
const char* paginaWeb PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Panel de Voz - VitalPet</title>
  <style>
    body { font-family: 'Segoe UI', Tahoma, sans-serif; background-color: #0f172a; color: white; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; }
    .card { background: #1e293b; padding: 2rem; border-radius: 12px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); text-align: center; max-width: 400px; width: 90%; }
    h2 { color: #38bdf8; margin-bottom: 0.5rem; }
    p { color: #94a3b8; font-size: 0.9rem; }
    input[type="file"] { margin: 20px 0; color: #cbd5e1; }
    button { background: #059669; color: white; border: none; padding: 12px 24px; font-size: 16px; font-weight: bold; border-radius: 6px; cursor: pointer; width: 100%; transition: 0.2s; }
    button:hover { background: #047857; }
  </style>
</head>
<body>
  <div class="card">
    <h2>🐾 VitalPet - Carga de Voz</h2>
    <p>Sube el nuevo audio de llamada para tu mascota (Formato MP3, maximo 1MB, Mono).</p>
    <form method="POST" action="/upload" enctype="multipart/form-data">
      <input type="file" name="update" accept=".mp3,audio/*" required>
      <button type="submit">Actualizar Voz en Bebedero</button>
    </form>
  </div>
</body>
</html>
)rawliteral";

const char* paginaDashboard PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Dashboard Clínico VitalPet</title>
  <style>
    body { font-family: 'Segoe UI', Tahoma, sans-serif; background-color: #0f172a; color: white; margin: 0; padding: 25px 20px; }
    .header { text-align: center; margin-bottom: 30px; }
    h1 { color: #38bdf8; margin: 0; font-size: 2.5rem; }
    p { color: #94a3b8; font-size: 1.1rem; margin-top: 5px; }
    .grid-container { display: flex; flex-wrap: wrap; justify-content: center; gap: 25px; max-width: 1100px; margin: 0 auto; }
    .card { background: #1e293b; padding: 20px; border-radius: 12px; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.5); text-align: center; width: 480px; max-width: 100%; box-sizing: border-box; }
    .card h3 { margin-top: 0; font-size: 1.25rem; margin-bottom: 15px; }
    iframe { border: none; border-radius: 8px; width: 100%; height: 280px; }
  </style>
</head>
<body>
  <div class="header">
    <h1>🐾 VitalPet Analytics</h1>
    <p>Telemetría Clínica en Tiempo Real (Ultimas 24 horas)</p>
  </div>
  <div class="grid-container">
    <div class="card">
      <h3 style="color: #38bdf8;">💧 Nivel de Tanque (mL)</h3>
      <iframe src="https://thingspeak.com/channels/%CHANNEL_ID%/charts/1?bgcolor=%231e293b&color=%2338bdf8&dynamic=true&days=1&type=line&update=15"></iframe>
    </div>
    <div class="card">
      <h3 style="color: #10b981;">🧪 Pureza del Agua (TDS ppm)</h3>
      <iframe src="https://thingspeak.com/channels/%CHANNEL_ID%/charts/2?bgcolor=%231e293b&color=%2310b981&dynamic=true&days=1&type=line&update=15"></iframe>
    </div>
    <div class="card">
      <h3 style="color: #ef4444;">🌡️ Temperatura (°C)</h3>
      <iframe src="https://thingspeak.com/channels/%CHANNEL_ID%/charts/3?bgcolor=%231e293b&color=%23ef4444&dynamic=true&days=1&type=line&update=15"></iframe>
    </div>
  </div>
</body>
</html>
)rawliteral";

void manejarSubida(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    Serial.printf("📥 Recibiendo archivo web: %s -> Guardando en memoria interna...\n", filename.c_str());
    if (LittleFS.exists(ARCHIVO_VOZ)) { LittleFS.remove(ARCHIVO_VOZ); }
    request->_tempFile = LittleFS.open(ARCHIVO_VOZ, "w");
  }
  if (len && request->_tempFile) { request->_tempFile.write(data, len); }
  if (final) {
    if(request->_tempFile) { request->_tempFile.close(); }
    Serial.println("✅ Audio guardado correctamente en LittleFS.");
    reproducirAudioNuevo = true;
  }
}

void iniciarServidorWeb() {
  servidorWebAudio.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", paginaWeb);
  });
  servidorWebAudio.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request){
    String dashboard = FPSTR(paginaDashboard);
    dashboard.replace("%CHANNEL_ID%", String(CHANNEL_ID));
    request->send(200, "text/html", dashboard);
  });
  
  // <-- CÓDIGO HTML CORREGIDO: Sin caracteres especiales y con meta charset UTF-8
  servidorWebAudio.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'></head>";
    html += "<body style='font-family:sans-serif; text-align:center; padding:50px; background:#0f172a; color:white;'>";
    html += "<h2 style='color:#10b981;'>[OK] Voz actualizada con exito</h2>";
    html += "<p>El bebedero esta reproduciendo la prueba de sonido.</p>";
    html += "<button onclick='history.back()' style='padding:10px 20px; background:#38bdf8; border:none; border-radius:6px; cursor:pointer; color:#0f172a; font-weight:bold;'>Volver</button>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  }, manejarSubida);
  
  servidorWebAudio.begin();
}

// ====================================================================
// 4. INTERRUPCIÓN HARDWARE (SENSOR ULTRASÓNICO)
// ====================================================================
volatile unsigned long echo_start = 0;
volatile unsigned long echo_end = 0;
volatile bool nueva_distancia_lista = false;

void IRAM_ATTR isr_ultrasonico_echo() {
    if (digitalRead(PIN_ECHO) == HIGH) {
        echo_start = micros();
    } else {
        echo_end = micros();
        nueva_distancia_lista = true;
    }
}

// ====================================================================
// 5. NÚCLEO 0: TELEGRAM BOT
// ====================================================================
void TareaTelegram(void *pvParameters) {
    unsigned long ultima_vez = 0;
    for (;;) {
        if (millis() - ultima_vez > 3000) {
            if (xSemaphoreTake(mutexRed, TIMEOUT_MUTEX) == pdTRUE) {
                int num_mensajes = bot.getUpdates(bot.last_message_received + 1);
                while (num_mensajes) {
                    for (int i = 0; i < num_mensajes; i++) {
                        String chat_id = bot.messages[i].chat_id;
                        String texto = bot.messages[i].text;
                        
                        if (id_del_chat != chat_id) {
                            id_del_chat = chat_id;
                            escribirStringEnEEPROM(400, id_del_chat);
                        }

                        if (texto == "/configurar") {
                            pasoConfigTelegram = 1;
                            bot.sendMessage(chat_id, "🐾 *ASISTENTE NUTRICIONAL VITALPET*\n\n¿Qué animal vamos a monitorear?\nEscribe *Perro* o *Gato*:", "Markdown");
                        }
                        else if (pasoConfigTelegram == 1) {
                            if (texto.equalsIgnoreCase("Perro")) { perfil.especie = 'P'; pasoConfigTelegram = 2; }
                            else if (texto.equalsIgnoreCase("Gato")) { perfil.especie = 'G'; pasoConfigTelegram = 2; }
                            else { bot.sendMessage(chat_id, "⚠️ No entendí. Escribe 'Perro' o 'Gato':", ""); continue; }
                            bot.sendMessage(chat_id, "Perfecto. ¿Es *Macho* o *Hembra*?", "Markdown");
                        }
                        else if (pasoConfigTelegram == 2) {
                            if (texto.equalsIgnoreCase("Macho")) { perfil.sexo = 'M'; pasoConfigTelegram = 3; }
                            else if (texto.equalsIgnoreCase("Hembra")) { perfil.sexo = 'H'; pasoConfigTelegram = 3; }
                            else { bot.sendMessage(chat_id, "⚠️ Escribe 'Macho' o 'Hembra':", ""); continue; }
                            bot.sendMessage(chat_id, "¡Anotado! Ahora escribe su *peso corporal en kg* (Ejemplo: 4.5):", "Markdown");
                        }
                        else if (pasoConfigTelegram == 3) {
                            float p = texto.toFloat();
                            if (p > 0.2 && p < 75.0) {
                                perfil.peso = p;
                                float rer_base = 70.0 * pow(perfil.peso, 0.75);
                                float k_esp = (perfil.especie == 'P') ? 1.6 : 1.2;
                                float c_cond = (perfil.especie == 'G' && perfil.sexo == 'M') ? 1.3 : 1.0;
                                
                                perfil.v_req = rer_base * k_esp * c_cond;
                                perfil.configurado = 1;
                                guardarPerfilEEPROM();
                                pasoConfigTelegram = 0;

                                String exito = "🎉 *¡OBJETIVO CLÍNICO CALCULADO!*\n\n";
                                exito += (perfil.especie == 'G') ? "🐱 Gato " : "🐶 Perro ";
                                exito += (perfil.sexo == 'M') ? "Macho | " : "Hembra | ";
                                exito += String(perfil.peso, 1) + " kg\n\n";
                                exito += "🎯 *Ingesta Diaria Requerida:* *" + String(perfil.v_req, 0) + " mL*";
                                bot.sendMessage(chat_id, exito, "Markdown");
                            } else { bot.sendMessage(chat_id, "⚠️ Peso irreal. Escribe un número válido:", ""); }
                        }
                        else if (texto == "/calibrar") {
                            pasoConfigTelegram = 10;
                            bot.sendMessage(chat_id, "⚖️ *CALIBRACIÓN DE BALANZA*\n\n"
                                                     "⚠️ *IMPORTANTE:* Antes de calibrar, asegúrate de que el contenedor esté vacío (solo con bomba y sensores adentro) y ejecuta `/setBalanzaCero`.\n\n"
                                                     "Luego, llena un volumen real conocido de agua en el contenedor (ej. `500` mL usando un vaso medidor) y colócalo.\n\n"
                                                     "Escribe únicamente el número en mL:", "Markdown");
                        }
                        else if (pasoConfigTelegram == 10) {
                            float vol_real = texto.toFloat();
                            if (vol_real > 10.0 && vol_real < 5000.0) {
                                volumenCalibracion = vol_real;
                                solicitarCalibracion = true; 
                                bot.sendMessage(chat_id, "⏳ Leyendo sensor HX711... Por favor no toques el bebedero.", "");
                                pasoConfigTelegram = 0;
                            } else {
                                bot.sendMessage(chat_id, "⚠️ Volumen irreal. Escribe un número válido mayor a 10 (ejemplo: 250):", "");
                            }
                        }
                        else if (texto == "/cambioAgua") {
                            modoCambioAgua = true;
                            bombaEncendida = false; 
                            String msg = "🔄 *MODO CAMBIO DE AGUA ACTIVADO*\n\n"
                                         "1. La bomba se ha *APAGADO* por seguridad para evitar que trabaje en seco.\n"
                                         "2. Conteo de consumo diario y lecturas de peso se encuentran *PAUSADOS*.\n\n"
                                         "👉 Puedes retirar el contenedor, lavarlo, rellenarlo y volver a colocar la tapa presionándola firmemente.\n\n"
                                         "Cuando todo esté asentado en su lugar, envía `/finCambio`.";
                            bot.sendMessage(chat_id, msg, "Markdown");
                        }
                        else if (texto == "/finCambio") {
                            if (!modoCambioAgua) {
                                bot.sendMessage(chat_id, "⚠️ El modo cambio de agua no estaba activo. Envía `/cambioAgua` para iniciarlo.", "");
                            } else {
                                bot.sendMessage(chat_id, "⏳ Estabilizando peso de nuevo... No toques el bebedero por 3 segundos.", "");
                                solicitarFinCambio = true;
                            }
                        }
                        else if (texto == "/status") {
                            String est = "No";
                            if (gatoDetectadoEsteCiclo) est = "Si (" + String(distanciaGlobal, 0) + " cm)";
                            
                            struct tm timeinfo;
                            String horaTxt = "Desincronizada";
                            if (getLocalTime(&timeinfo, 10)) {
                                horaTxt = String(timeinfo.tm_hour) + ":" + (timeinfo.tm_min < 10 ? "0" : "") + String(timeinfo.tm_min);
                            }

                            String msg = "📊 *CENTRAL VITALPET*\n\n";
                            msg += "🕒 Hora local: *" + horaTxt + "*\n";
                            msg += "⏰ Alarma diaria: *" + String(perfil.hora_alerta) + ":" + (perfil.minuto_alerta < 10 ? "0" : "") + String(perfil.minuto_alerta) + "*\n\n";
                            msg += "💧 Nivel Tanque: " + String(pesoGlobal, 0) + " mL\n";
                            msg += "📈 Consumo hoy: *" + String(consumoHoy, 0) + " mL*\n";
                            msg += "🎯 Meta del día: " + (perfil.configurado ? (String(perfil.v_req, 0) + " mL") : "No configurada ⚠️") + "\n\n";
                            msg += "🌡️ Temp: " + String(tempGlobal, 1) + " °C | 🧪 TDS: " + String(tdsGlobal, 0) + " ppm\n";
                            msg += "🐱 Mascota en plato: " + est + "\n";
                            msg += "⚙️ Bomba: " + String(bombaEncendida ? "ACTIVA 💧" : "APAGADA 🛑");
                            bot.sendMessage(chat_id, msg, "Markdown");
                        }
                        else if (texto == "/llamar") {
                            String infoLlamada = "📢 *LLAMANDO A TU MASCOTA*\n\n";
                            infoLlamada += "💧 Nivel actual: " + String(pesoGlobal, 0) + " mL\n";
                            infoLlamada += "📈 Consumo hoy: " + String(consumoHoy, 0) + " mL\n";
                            infoLlamada += "🌡️ Temperatura: " + String(tempGlobal, 1) + " °C\n";
                            infoLlamada += "🧪 Calidad TDS: " + String(tdsGlobal, 0) + " ppm\n\n";
                            infoLlamada += "🔊 Reproduciendo audio en el altavoz...";
                            bot.sendMessage(chat_id, infoLlamada, "Markdown");
                            solicitarReproduccion = true;
                        }
                        else if (texto == "/cambiarvoz" || texto == "/dashboards") {
                            String ip = WiFi.localIP().toString();
                            String msg = "🌐 *PANEL WEB Y DASHBOARD VITALPET*\n\n";
                            msg += "Abre estos enlaces en tu navegador (mismo Wi-Fi):\n\n";
                            msg += "🎙️ *Subir Voz MP3:* http://vitalpet.local:8080\n";
                            msg += "📊 *Dashboard Clínico:* http://vitalpet.local:8080/dashboard\n\n";
                            msg += "*(Si tu red no soporta .local, usa la IP)*:\n";
                            msg += "👉 http://" + ip + ":8080\n";
                            msg += "👉 http://" + ip + ":8080/dashboard";
                            bot.sendMessage(chat_id, msg, "Markdown");
                        }
                        else if (texto.startsWith("/recordatorio")) {
                            int espacio1 = texto.indexOf(' ');
                            int espacio2 = texto.lastIndexOf(' ');
                            if (espacio1 != -1 && espacio2 != -1 && espacio1 != espacio2) {
                                int h = texto.substring(espacio1 + 1, espacio2).toInt();
                                int m = texto.substring(espacio2 + 1).toInt();
                                if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
                                    perfil.hora_alerta = h;
                                    perfil.minuto_alerta = m;
                                    guardarPerfilEEPROM();
                                    String conf = "⏰ *¡RECORDATORIO PROGRAMADO!*\n\n";
                                    conf += "Recibirás el resumen clínico de hidratación todos los días a las *" + String(h < 10 ? "0" : "") + String(h) + ":" + String(m < 10 ? "0" : "") + String(m) + "*.\n";
                                    bot.sendMessage(chat_id, conf, "Markdown");
                                } else {
                                    bot.sendMessage(chat_id, "⚠️ Hora o minuto inválido. Usa formato 24h: `/recordatorio 20 30`", "Markdown");
                                }
                            } else {
                                bot.sendMessage(chat_id, "ℹ️ *Configurar Alarma Diaria*\nPara programar el aviso envía el comando seguido de la hora y minuto en formato 24 horas.\n\n👉 *Ejemplo:* `/recordatorio 20 30` (Para las 8:30 PM)", "Markdown");
                            }
                        }
                        else if (texto == "/setBalanzaCero") {
                            solicitarTara = true;
                            bot.sendMessage(chat_id, "⏳ Calibrando balanza a 0 mL... Por favor no toques el plato.", "");
                        }
                        else if (texto == "/resetwifi") {
                            bot.sendMessage(chat_id, "⚠️ Borrando credenciales Wi-Fi y reiniciando en Modo AP...", "");
                            bot.getUpdates(bot.last_message_received + 1);
                            delay(1500);
                            borrarCredencialesEEPROM();
                            delay(500);
                            ESP.restart();
                        }
                        else if (texto == "/resetconsumo") {
                            consumoHoy = 0.0;
                            ultimoPesoEstable = pesoGlobal;
                            bot.sendMessage(chat_id, "✅ Contador de consumo diario reiniciado a 0 mL para demostración.", "");
                        }
                        else if (texto == "/help" || pasoConfigTelegram == 0) {
                            String msgHelp = "🐾 *VitalPet V3 - Menú Principal*\n\n";
                            msgHelp += "/configurar - Ingresar perfil de mascota\n";
                            msgHelp += "/status - Ver telemetría en tiempo real\n";
                            msgHelp += "/recordatorio - Configurar hora de reporte diario\n";
                            msgHelp += "/llamar - Forzar altavoz y ver datos\n";
                            msgHelp += "/dashboards - Abrir Dashboard Clínico y Panel de Voz 📊\n";
                            msgHelp += "/cambioAgua - Iniciar mantenimiento/relleno de agua\n";
                            msgHelp += "/finCambio - Terminar cambio de agua y estabilizar\n";
                            msgHelp += "/setBalanzaCero - Calibrar peso vacío a 0 mL (Guardar Tara)\n";
                            msgHelp += "/resetwifi - Borrar Wi-Fi y activar Modo AP\n";
                            msgHelp += "/help - Ver este menú de ayuda";
                            
                            bot.sendMessage(chat_id, msgHelp, "Markdown");
                        }
                    }
                    num_mensajes = bot.getUpdates(bot.last_message_received + 1);
                }
                xSemaphoreGive(mutexRed);
            }
            ultima_vez = millis();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ====================================================================
// 6. FUNCIONES DE SENSADO, AUDIO HÍBRIDO, NUBE Y ALERTAS
// ====================================================================
void manejarAudio() {
    if (solicitarReproduccion || reproducirAudioNuevo) {
        solicitarReproduccion = false;
        reproducirAudioNuevo = false;
        
        if (mp3->isRunning()) mp3->stop();
        if (fileProgmem != NULL) { fileProgmem->close(); delete fileProgmem; fileProgmem = NULL; }
        if (fileLittleFS != NULL) { fileLittleFS->close(); delete fileLittleFS; fileLittleFS = NULL; }
        
        if (LittleFS.exists(ARCHIVO_VOZ)) {
            Serial.println("🎵 Reproduciendo voz personalizada desde LittleFS...");
            fileLittleFS = new AudioFileSourceLittleFS(ARCHIVO_VOZ);
            mp3->begin(fileLittleFS, out);
        } else if (sizeof_audio_dueno > 0) {
            Serial.println("🎵 Archivo no encontrado en LittleFS. Reproduciendo audio de fábrica (PROGMEM)...");
            fileProgmem = new AudioFileSourcePROGMEM(audio_dueno, sizeof_audio_dueno);
            mp3->begin(fileProgmem, out);
        } else {
            Serial.println("⚠️ No hay audio personalizado ni audio de fábrica configurado.");
            return;
        }
    }
    
    if (mp3 && mp3->isRunning()) {
        if (!mp3->loop()) {
            mp3->stop();
            Serial.println("⏹️ Reproducción finalizada.");
        }
    }
}

void manejarBomba() {
    if (modoCambioAgua) {
        if (bombaEncendida) { bombaEncendida = false; pinMode(PIN_BOMBA, INPUT); }
        return;
    }
    if (tdsGlobal > TDS_MAXIMO) {
        if (bombaEncendida) { bombaEncendida = false; pinMode(PIN_BOMBA, INPUT); }
        return; 
    }
    if (gatoDetectadoEsteCiclo && !bombaEncendida) {
        bombaEncendida = true; pinMode(PIN_BOMBA, OUTPUT); digitalWrite(PIN_BOMBA, LOW); 
    } 
    else if (!gatoDetectadoEsteCiclo && bombaEncendida) {
        bombaEncendida = false; pinMode(PIN_BOMBA, INPUT); 
    }
}

// <-- INTEGRACIÓN: LECTURA DE TEMPERATURA ACTUALIZADA
void leerTemperatura() {
    if (!sensorTempDetectado) {
        tempGlobal = 25.0; // Valor seguro de respaldo
        return;
    }
    sensorTemp.requestTemperatures();
    float lectura = sensorTemp.getTempCByIndex(0);
    
    if (lectura == DEVICE_DISCONNECTED_C || lectura == 85.0) {
        tempGlobal = 25.0; // Fallback si se desconecta
    } else {
        tempGlobal = lectura;
    }
}

// <-- INTEGRACIÓN: LECTURA REAL DE TDS CON COMPENSACIÓN
void leerTDS() {
    long sumaAnaloga = 0;
    for (int i = 0; i < 10; i++) {
        sumaAnaloga += analogRead(PIN_TDS);
        delay(2);
    }
    float valorAnalogoPromedio = sumaAnaloga / 10.0;
    
    float voltaje = valorAnalogoPromedio * (VREF / (float)RESOLUCION_ADC);
    
    // Aplicamos la compensación basada en tempGlobal
    float voltajeCompensado = voltaje / (1.0 + 0.02 * (tempGlobal - 25.0));
    
    tdsGlobal = (133.42 * pow(voltajeCompensado, 3) - 255.86 * pow(voltajeCompensado, 2) + 857.39 * voltajeCompensado) * 0.5;
    
    if (tdsGlobal < 0) tdsGlobal = 0;
}

void enviarThingSpeak() {
    if (xSemaphoreTake(mutexRed, TIMEOUT_MUTEX) == pdTRUE) {
        ThingSpeak.setField(1, pesoGlobal);
        ThingSpeak.setField(2, tdsGlobal);
        ThingSpeak.setField(3, tempGlobal);
        if (gatoDetectadoEsteCiclo) ThingSpeak.setField(4, distanciaGlobal);

        int res = ThingSpeak.writeFields(CHANNEL_ID, WRITE_API_KEY);
        if (res == 200) {
            Serial.println("📊 ThingSpeak OK -> Peso: " + String(pesoGlobal, 0) + "mL | TDS: " + String(tdsGlobal, 0) + " | Temp: " + String(tempGlobal, 1));
        } else { Serial.println("⚠️ Error ThingSpeak: " + String(res)); }
        xSemaphoreGive(mutexRed);
    }
}

void revisarAlertas() {
    if (id_del_chat == "") return;

    if (pesoGlobal < 300.0) {
        if (!tanqueEstabaBajo || millis() - ultimaAlerta > tiempoEsperaAlerta) {
            if (xSemaphoreTake(mutexRed, TIMEOUT_MUTEX) == pdTRUE) {
                ultimaAlerta = millis(); tanqueEstabaBajo = true;
                bot.sendMessage(id_del_chat, "⚠️ *ALERTA*: Tanque bajo (" + String(pesoGlobal, 0) + " mL). Por favor rellena el recipiente.", "Markdown");
                xSemaphoreGive(mutexRed);
            }
        }
    } else if (pesoGlobal > 350.0) {
        tanqueEstabaBajo = false;
    }

    if (tdsGlobal > TDS_MAXIMO) {
        if (!tdsEstabaMalo || millis() - ultimaAlertaTDS > tiempoEsperaAlertaTDS) {
            if (xSemaphoreTake(mutexRed, TIMEOUT_MUTEX) == pdTRUE) {
                ultimaAlertaTDS = millis(); tdsEstabaMalo = true;
                bot.sendMessage(id_del_chat, "🧪 *ALERTA DE SEGURIDAD*\nTDS crítico (" + String(tdsGlobal, 0) + " ppm).\nBomba bloqueada por protección de la mascota.", "Markdown");
                xSemaphoreGive(mutexRed);
            }
        }
    } else if (tdsGlobal < TDS_MAXIMO - 30.0) {
        tdsEstabaMalo = false;
    }
}

void auditarHidratacionClinica() {
    if (!perfil.configurado || id_del_chat == "") return;
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) return;
    
    int horaActual = timeinfo.tm_hour;
    int minutoActual = timeinfo.tm_min;

    if (horaActual == 0 && minutoActual == 0) {
        if (!reporteEnviadoHoy) {
            consumoHoy = 0.0;
            reporteEnviadoHoy = false;
        }
    }

    if (horaActual == perfil.hora_alerta && minutoActual == perfil.minuto_alerta && !reporteEnviadoHoy) {
        reporteEnviadoHoy = true;
        
        String reporte = "📋 *RESUMEN CLÍNICO DIARIO VITALPET*\n";
        reporte += "⏰ Fecha y Hora: " + String(timeinfo.tm_mday) + "/" + String(timeinfo.tm_mon + 1) + " - " + String(horaActual) + ":" + (minutoActual < 10 ? "0" : "") + String(minutoActual) + "\n\n";
        reporte += "🎯 Meta requerida: *" + String(perfil.v_req, 0) + " mL*\n";
        reporte += "📈 Consumo de hoy: *" + String(consumoHoy, 0) + " mL*\n";
        reporte += "💧 Nivel en tanque: *" + String(pesoGlobal, 0) + " mL*\n\n";

        if (consumoHoy >= perfil.v_req) {
            reporte += "🌟 *¡EXCELENTE HIDRATACIÓN!*\nTu mascota cumplió o superó sus requerimientos de agua hoy.";
            if (xSemaphoreTake(mutexRed, TIMEOUT_MUTEX) == pdTRUE) {
                bot.sendMessage(id_del_chat, reporte, "Markdown");
                xSemaphoreGive(mutexRed);
            }
        } else {
            float deficit = perfil.v_req - consumoHoy;
            if (deficit < 0) deficit = 0;
            reporte += "⚠️ *ALERTA MÉDICA: DÉFICIT DETECTADO*\n";
            reporte += "A tu mascota le faltaron aproximadamente *" + String(deficit, 0) + " mL* para cumplir su meta vital.\n\n";
            reporte += "📢 *Acción:* Disparando llamada acústica de atracción en el bebedero...";
            if (xSemaphoreTake(mutexRed, TIMEOUT_MUTEX) == pdTRUE) {
                bot.sendMessage(id_del_chat, reporte, "Markdown");
                xSemaphoreGive(mutexRed);
            }
            solicitarReproduccion = true;
        }
    }
}

// ====================================================================
// 7. MAIN C NATIVO
// ====================================================================
int main(void) {
    Serial.begin(115200);
    delay(1000);
    
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    pinMode(PIN_BOMBA, INPUT);

    intentoconexion(VITALPET_AP_SSID, VITALPET_AP_PASSWORD);

    if (!LittleFS.begin(true)) {
        Serial.println("❌ Error montando LittleFS. Solo funcionará audio de fábrica.");
    } else {
        Serial.println("📂 LittleFS montado correctamente.");
    }

    iniciarServidorWeb();

    if (!MDNS.begin("vitalpet")) {
        Serial.println("❌ Error iniciando mDNS");
    } else {
        Serial.println("🌐 Dominio mDNS iniciado: http://vitalpet.local:8080");
        MDNS.addService("http", "tcp", 8080); 
    }

    mutexRed = xSemaphoreCreateMutex();
    
    cargarPerfilEEPROM();
    String chat_guardado = leerStringDeEEPROM(400);
    if(chat_guardado.length() > 5) id_del_chat = chat_guardado;

    attachInterrupt(digitalPinToInterrupt(PIN_ECHO), isr_ultrasonico_echo, CHANGE);

    configTime(-18000, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("⏳ Sincronizando reloj con el servidor atómico (Ecuador UTC-5)...");

    secured_client.setTimeout(5000); secured_client.setInsecure();
    clientThingSpeak.setTimeout(5000);
    ThingSpeak.begin(clientThingSpeak);

    balanza.begin(PIN_DOUT, PIN_SCK);
    balanza.set_scale(perfil.factor_escala);
    balanza.set_offset(perfil.tare_offset);
    Serial.printf("⚖️ Balanza inicializada: offset = %d, escala = %.4f\n", perfil.tare_offset, perfil.factor_escala);
    
    if (balanza.is_ready()) {
        float lectura_inicial = balanza.get_units(10);
        pesoGlobal = lectura_inicial;
        if (pesoGlobal < 3.0) pesoGlobal = 0.0;
        ultimoPesoEstable = pesoGlobal;
        Serial.printf("💧 Nivel de agua inicial: %.1f mL\n", pesoGlobal);
    }
    
    sensorTemp.begin(); sensorTemp.setWaitForConversion(false);
    if (sensorTemp.getDeviceCount() > 0) sensorTempDetectado = true;

    out = new AudioOutputI2S();
    out->SetPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);
    out->SetGain(0.60);
    mp3 = new AudioGeneratorMP3();

    xTaskCreatePinnedToCore(TareaTelegram, "Telegram", 8192, NULL, 1, NULL, 0);

    unsigned long ultimo_disparo_trig = 0;

    while (true) {
        manejarAudio();
        if (mp3 && mp3->isRunning()) {
            delay(1);
            continue;
        }
        
        if (solicitarTara) {
            solicitarTara = false;
            balanza.tare(20);
            perfil.tare_offset = balanza.get_offset();
            guardarPerfilEEPROM(); 
            pesoGlobal = 0.0;
            ultimoPesoEstable = 0.0;
            if (xSemaphoreTake(mutexRed, TIMEOUT_MUTEX) == pdTRUE) {
                bot.sendMessage(id_del_chat, "✅ Balanza calibrada a 0 mL correctamente y guardada en EEPROM.", "");
                xSemaphoreGive(mutexRed);
            }
        }

        if (solicitarCalibracion) {
            solicitarCalibracion = false;
            float valor_crudo = balanza.get_value(20);
            if (valor_crudo != 0 && volumenCalibracion > 0) {
                perfil.factor_escala = valor_crudo / volumenCalibracion;
                balanza.set_scale(perfil.factor_escala);
                guardarPerfilEEPROM();
                pesoGlobal = volumenCalibracion;
                ultimoPesoEstable = volumenCalibracion;
                
                String exitoCal = "✅ *¡CALIBRACIÓN EXITOSA!*\n\n";
                exitoCal += "Nuevo factor de escala calculado y guardado en EEPROM: `" + String(perfil.factor_escala, 2) + "`\n\n";
                exitoCal += "El tanque ahora marca exactamente: *" + String(volumenCalibracion, 0) + " mL*.";
                if (xSemaphoreTake(mutexRed, TIMEOUT_MUTEX) == pdTRUE) {
                    bot.sendMessage(id_del_chat, exitoCal, "Markdown");
                    xSemaphoreGive(mutexRed);
                }
            } else {
                if (xSemaphoreTake(mutexRed, TIMEOUT_MUTEX) == pdTRUE) {
                    bot.sendMessage(id_del_chat, "❌ Error al leer el sensor HX711. Inténtalo de nuevo con /calibrar.", "");
                    xSemaphoreGive(mutexRed);
                }
            }
        }

        if (solicitarFinCambio) {
            solicitarFinCambio = false;
            delay(3000); 
            if (balanza.is_ready()) {
                float lectura_nueva = balanza.get_units(10); 
                pesoGlobal = lectura_nueva;
                if (pesoGlobal < 3.0) pesoGlobal = 0.0;
                ultimoPesoEstable = pesoGlobal; 
            }
            modoCambioAgua = false; 
            String finMsg = "✅ *CAMBIO DE AGUA FINALIZADO*\n\n"
                            "Nivel de agua detectado: *" + String(pesoGlobal, 0) + " mL*\n"
                            "Bomba reactivada y conteo de consumo reanudado.";
            if (xSemaphoreTake(mutexRed, TIMEOUT_MUTEX) == pdTRUE) {
                bot.sendMessage(id_del_chat, finMsg, "Markdown");
                xSemaphoreGive(mutexRed);
            }
        }

        if (micros() - ultimo_disparo_trig > 100000) {
            ultimo_disparo_trig = micros();
            digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10); digitalWrite(PIN_TRIG, LOW);
        }

        if (nueva_distancia_lista) {
            nueva_distancia_lista = false;
            float medicion_actual = (echo_end - echo_start) * 0.0343 / 2.0;
            
            if (medicion_actual > 2.0 && medicion_actual < 15.0) {
                contadorFiltroGato++;
                if(contadorFiltroGato > 2) {
                    gatoDetectadoEsteCiclo = true;
                    tiempoUltimoGato = millis();
                }
            } else {
                contadorFiltroGato = 0;
                if (millis() - tiempoUltimoGato > 3000) {
                    gatoDetectadoEsteCiclo = false;
                }
            }
            distanciaGlobal = medicion_actual;
        }

        if (balanza.is_ready()) {
            float lectura_nueva = balanza.get_units(2);
            pesoGlobal = (0.10 * lectura_nueva) + (0.90 * pesoGlobal);
            
            if(pesoGlobal < 3.0) {
                pesoGlobal = 0.0;
            }
            
            if (!modoCambioAgua) {
                if (gatoDetectadoEsteCiclo && !gatoEstabaPresente) {
                    gatoEstabaPresente = true;
                    pesoAntesDelGato = ultimoPesoEstable;
                }
                else if (!gatoDetectadoEsteCiclo && gatoEstabaPresente) {
                    gatoEstabaPresente = false;
                    if (pesoAntesDelGato >= 0.0) {
                        float deltaGato = pesoAntesDelGato - pesoGlobal;
                        if (deltaGato >= 3.0) {
                            consumoHoy += deltaGato;
                            ultimoPesoEstable = pesoGlobal;
                        } else if (deltaGato <= -15.0) {
                            ultimoPesoEstable = pesoGlobal;
                        } else {
                            ultimoPesoEstable = pesoGlobal;
                        }
                    }
                }
                else if (!gatoDetectadoEsteCiclo && !gatoEstabaPresente) {
                    if (ultimoPesoEstable < 0.0 && pesoGlobal > 0.0) {
                        ultimoPesoEstable = pesoGlobal;
                    }
                    if (ultimoPesoEstable >= 0.0) {
                        float deltaConsumo = ultimoPesoEstable - pesoGlobal;
                        if (deltaConsumo >= 3.0) { 
                            consumoHoy += deltaConsumo;
                            ultimoPesoEstable = pesoGlobal;
                        } else if (deltaConsumo <= -15.0) { 
                            ultimoPesoEstable = pesoGlobal;
                        }
                    }
                }
            }
        }

        if (millis() - ultimaLecturaTemp > 10000) { ultimaLecturaTemp = millis(); leerTemperatura(); }
        if (millis() - ultimaLecturaTDS > 10000) { ultimaLecturaTDS = millis(); leerTDS(); }
        
        if (millis() - ultimoEnvioTS > tiempoEsperaTS) {
            ultimoEnvioTS = millis();
            enviarThingSpeak();
        }

        manejarBomba();
        revisarAlertas();
        auditarHidratacionClinica();

        delay(1);
    }
    return 0;
}

void setup() { main(); }
void loop() {}
