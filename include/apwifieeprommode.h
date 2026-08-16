#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif
#include <Wire.h>

WebServer server(80);

String leerStringDeEEPROM(int direccion)
{
    String cadena = "";
    char caracter = EEPROM.read(direccion);
    int i = 0;
    while (caracter != '\0' && i < 100)
    {
        cadena += caracter;
        i++;
        caracter = EEPROM.read(direccion + i);
    }
    return cadena;
}

void escribirStringEnEEPROM(int direccion, String cadena)
{
    int longitudCadena = cadena.length();
    for (int i = 0; i < longitudCadena; i++)
    {
        EEPROM.write(direccion + i, cadena[i]);
    }
    EEPROM.write(direccion + longitudCadena, '\0'); // Null-terminated string
    EEPROM.commit();                                // Guardamos los cambios en la memoria EEPROM
}

void borrarCredencialesEEPROM()
{
    Serial.println("Borrando credenciales Wi-Fi de la EEPROM...");
    // Borramos los 2 slots de memoria donde se guardan SSID y Contraseña
    escribirStringEnEEPROM(0, "");
    escribirStringEnEEPROM(50, "");
    escribirStringEnEEPROM(100, "");
    escribirStringEnEEPROM(150, "");
    
    // Borramos el puntero de última red guardada
    escribirStringEnEEPROM(300, "");
    Serial.println("Memoria Wi-Fi limpia.");
}
// =========================================================
// DISEÑO CSS Y HTML (ESTILO VITALPET DARK MODE)
// =========================================================
const char* paginaWifiRoot PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>VitalPet - Wi-Fi Setup</title>
  <style>
    body { font-family: 'Segoe UI', Tahoma, sans-serif; background-color: #0f172a; color: white; display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 100vh; margin: 0; }
    .card { background: #1e293b; padding: 2.5rem; border-radius: 12px; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.5); text-align: center; max-width: 400px; width: 85%; }
    h2 { color: #38bdf8; margin-top: 0; margin-bottom: 0.5rem; }
    p { color: #94a3b8; font-size: 0.9rem; margin-bottom: 1.5rem; }
    .input-group { margin-bottom: 15px; text-align: left; }
    label { display: block; font-size: 0.85rem; color: #cbd5e1; margin-bottom: 5px; font-weight: 600; }
    input[type="text"], input[type="password"] { width: 100%; padding: 12px; border-radius: 6px; border: 1px solid #475569; background: #334155; color: white; font-size: 1rem; box-sizing: border-box; transition: 0.2s; }
    input[type="text"]:focus, input[type="password"]:focus { outline: none; border-color: #38bdf8; box-shadow: 0 0 0 3px rgba(56, 189, 248, 0.2); }
    button { background: #059669; color: white; border: none; padding: 12px 24px; font-size: 16px; font-weight: bold; border-radius: 6px; cursor: pointer; transition: 0.2s; width: 100%; margin-top: 10px; }
    button:hover { background: #047857; }
  </style>
</head>
<body>
  <div class="card">
    <h2>🐾 VitalPet Wi-Fi</h2>
    <p>Conecta el dispensador a tu red local para activar la telemetría y alertas a Telegram.</p>
    <form method="POST" action="/wifi">
      <div class="input-group">
        <label for="ssid">Red Wi-Fi (SSID)</label>
        <input type="text" id="ssid" name="ssid" required placeholder="Ej: MiCasa_2.4G">
      </div>
      <div class="input-group">
        <label for="password">Contraseña</label>
        <input type="password" id="password" name="password" placeholder="••••••••">
      </div>
      <button type="submit">Conectar Bebedero</button>
    </form>
  </div>
</body>
</html>
)rawliteral";

void handleRoot()
{
    server.send(200, "text/html", paginaWifiRoot);
}

int posW = 50;
void handleWifi()
{
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    Serial.print("Conectando a la red Wi-Fi ");
    Serial.println(ssid);
    Serial.println("Clave Wi-Fi recibida (oculta por seguridad)");
    Serial.print("...");
    WiFi.disconnect(); // Desconectar la red Wi-Fi anterior, si se estaba conectado
    WiFi.begin(ssid.c_str(), password.c_str(), 6);

    int cnt = 0;
    while (WiFi.status() != WL_CONNECTED and cnt < 8)
    {
        delay(1000);
        Serial.print(".");
        cnt++;
    }

    String htmlRespuesta = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Estado VitalPet</title><style>";
    htmlRespuesta += "body { font-family: 'Segoe UI', Tahoma, sans-serif; background-color: #0f172a; color: white; display: flex; align-items: center; justify-content: center; height: 100vh; margin: 0; }";
    htmlRespuesta += ".card { background: #1e293b; padding: 2.5rem; border-radius: 12px; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.5); text-align: center; max-width: 400px; width: 85%; }";
    htmlRespuesta += "button { padding: 10px 20px; font-size: 15px; font-weight: bold; border-radius: 6px; cursor: pointer; border: none; margin-top: 15px; width: 100%; transition: 0.2s; }";
    htmlRespuesta += ".btn-ok { background: #059669; color: white; } .btn-ok:hover { background: #047857; }";
    htmlRespuesta += ".btn-err { background: #0284c7; color: white; } .btn-err:hover { background: #0369a1; }";
    htmlRespuesta += "</style></head><body><div class='card'>";

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("Guardando en memoria eeprom...");
        String varsave = leerStringDeEEPROM(300);
        if (varsave == "a") {
            posW = 0;
            escribirStringEnEEPROM(300, "b");
        }
        else{
            posW=50;
            escribirStringEnEEPROM(300, "a");
        }
        escribirStringEnEEPROM(0 + posW, ssid);
        escribirStringEnEEPROM(100 + posW, password);

        Serial.println("Conexión establecida");
        
        htmlRespuesta += "<h2 style='color:#10b981; margin-top:0;'>✅ ¡Conectado con Éxito!</h2>";
        htmlRespuesta += "<p style='color:#94a3b8;'>El bebedero se enlazó correctamente a <b>" + ssid + "</b> y las credenciales se guardaron en la memoria.</p>";
        // <-- NUEVO: Mencionar el enlace local mDNS para que el usuario sepa que existe
        htmlRespuesta += "<p style='font-size:0.85rem; color:#38bdf8;'>Ya puedes cerrar esta ventana. Para configurar el audio o ver gráficas ingresa a:<br><br><b>http://vitalpet.local:8080</b></p>";
        htmlRespuesta += "</div></body></html>";
        
        server.send(200, "text/html", htmlRespuesta);
    }
    else
    {
        Serial.println("Conexión no establecida");
        
        htmlRespuesta += "<h2 style='color:#ef4444; margin-top:0;'>❌ Error de Conexión</h2>";
        htmlRespuesta += "<p style='color:#94a3b8;'>No pudimos conectar a <b>" + ssid + "</b>. Revisa que el nombre y la clave sean correctos y que la red sea 2.4 GHz.</p>";
        htmlRespuesta += "<button class='btn-err' onclick='history.back()'>Intentar de Nuevo</button>";
        htmlRespuesta += "</div></body></html>";
        
        server.send(200, "text/html", htmlRespuesta);
    }
}

bool lastRed()
{ 
    for (int psW = 0; psW <= 50; psW += 50)
    {
        String usu = leerStringDeEEPROM(0 + psW);
        String cla = leerStringDeEEPROM(100 + psW);
        Serial.println("Intentando conexión con una red guardada...");
        WiFi.disconnect();
        WiFi.begin(usu.c_str(), cla.c_str(), 6);
        int cnt = 0;
        while (WiFi.status() != WL_CONNECTED and cnt < 5)
        {
            delay(1000);
            Serial.print(".");
            cnt++;
        }
        if (WiFi.status() == WL_CONNECTED){
            Serial.println("Conectado a Red Wifi");
            Serial.println(WiFi.localIP());
            break;
        }
    }
    if (WiFi.status() == WL_CONNECTED)
        return true;
    else
        return false;
}

void initAP(const char *apSsid, const char *apPassword)
{ 
    Serial.begin(115200);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid, apPassword);

    server.on("/", handleRoot);
    server.on("/wifi", handleWifi);

    server.begin();
    Serial.println("Servidor web iniciado");
}

void loopAP()
{
    server.handleClient();
}

void intentoconexion(const char *apname, const char *appassword)
{
    Serial.begin(115200);
    EEPROM.begin(512);
    Serial.println("ingreso a intentoconexion");
    if (!lastRed())
    {                               
        Serial.println("Conectarse desde su celular a la red creada");
        Serial.println("en el navegador colocar la ip:");
        Serial.println("192.168.4.1");
        initAP(apname, appassword); 
    }
    while (WiFi.status() != WL_CONNECTED) 
    {
        loopAP(); 
    }

    // <--- NUEVO: GESTIÓN INTELIGENTE DE RECURSOS (Para cumplir con la diapositiva) --->
    Serial.println("✅ Conectado al Wi-Fi del hogar. Apagando Modo AP para liberar memoria...");
    server.stop(); // Apaga el servidor de configuración (puerto 80)
    WiFi.softAPdisconnect(true); // Destruye la red "VitalPet_Setup" oculta
    WiFi.mode(WIFI_STA); // Deja el chip exclusivamente en modo Estación (Cliente)
}
