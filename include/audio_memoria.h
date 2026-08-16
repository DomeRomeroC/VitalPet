#pragma once
#include <Arduino.h>

// Archivo público seguro.
// El proyecto original incluía aquí una grabación real codificada como bytes MP3.
// Esa voz no se publica para evitar exponer datos personales.
//
// VitalPet seguirá funcionando: el usuario puede cargar su propio MP3 desde
// http://vitalpet.local:8080 después de conectar el dispositivo a su Wi-Fi.
// Si se desea un audio de fábrica, convierta un MP3 propio a un arreglo PROGMEM
// y reemplace este contenido localmente.

const uint8_t audio_dueno[] PROGMEM = { 0x00 };
const unsigned int sizeof_audio_dueno = 0;
