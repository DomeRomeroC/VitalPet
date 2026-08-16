# VitalPet 🐾💧

VitalPet es un bebedero inteligente para mascotas basado en **ESP32**. El sistema monitorea el nivel y el consumo de agua, utiliza un sensor **TDS** como indicador de cambios en los sólidos disueltos, compensa la lectura con un **DS18B20** y detecta la aproximación de la mascota mediante un **HC-SR04**.

Cuando la mascota se acerca y las condiciones de operación son válidas, el ESP32 activa una minibomba de 5 V. Además, VitalPet se conecta por Wi-Fi para enviar telemetría y alertas mediante **Telegram** y **ThingSpeak**, y ofrece un dashboard local para visualizar los datos.

## ¿Cómo funciona?

1. La **celda de carga + HX711** estima el nivel de agua y permite registrar variaciones asociadas al consumo.
2. El **TDS** y el **DS18B20** supervisan las condiciones del agua.
3. El **HC-SR04** detecta si la mascota se encuentra frente al bebedero.
4. La **minibomba** se activa mediante un relé cuando se cumplen las condiciones programadas.
5. El bot de **Telegram** permite consultar el estado, configurar la mascota, recibir alertas, iniciar el modo de cambio de agua y reproducir una llamada de voz.
6. Los datos se envían a **ThingSpeak** y también se muestran en un dashboard local de 24 horas.

> **Nota:** el sensor TDS no detecta bacterias directamente; se usa como indicador de variaciones en los sólidos disueltos del agua.

## Hardware principal

- ESP32 de 38 pines
- Celda de carga + HX711
- Sensor TDS
- Sensor de temperatura DS18B20
- Sensor ultrasónico HC-SR04
- Minibomba DC 5 V + relé
- Amplificador MAX98357A + parlante de 3 W
- Power bank de 10000 mAh
- PCB y carcasa impresa en 3D

## Estructura del repositorio

```text
VitalPet_GitHub/
├── README.md
├── SECURITY.md
├── .gitignore
├── include/
│   ├── apwifieeprommode.h
│   ├── audio_memoria.h
│   └── secrets.h.example
└── src/
    └── main.cpp
```

## Configuración de credenciales

Las credenciales reales **no están incluidas** en el repositorio.

1. Copia `include/secrets.h.example` como `include/secrets.h`.
2. Completa localmente:
   - token del bot de Telegram;
   - Channel ID de ThingSpeak;
   - Write API Key de ThingSpeak;
   - contraseña de la red temporal de configuración de VitalPet.
3. No elimines la regla de `.gitignore` que excluye `include/secrets.h`.

El firmware tampoco imprime las contraseñas Wi-Fi guardadas en el monitor serial.

## Audio

La grabación original del dueño fue retirada de la versión pública. VitalPet permite cargar un archivo MP3 propio desde su interfaz web local una vez conectado a la red Wi-Fi.

## Mejoras futuras

- Añadir **dos sensores ultrasónicos laterales** para detectar a la mascota cuando se aproxime desde distintos ángulos.
- Incorporar una **cámara y un modelo de visión artificial** para reconocer al gato y analizar si está orientado o prestando atención al bebedero.
- Medir experimentalmente la autonomía real del sistema con el power bank instalado.

## Autores

**Stefano Peñaloza · Doménica Romero**  
Laboratorio de Sistemas Embebidos — ESPOL
