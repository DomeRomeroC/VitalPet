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

## Comandos de Telegram

VitalPet incorpora un bot de Telegram que permite configurar el sistema,
consultar la telemetría y ejecutar acciones de mantenimiento de forma remota.

| Comando | Función |
|---|---|
| `/help` | Muestra el menú principal de comandos disponibles. |
| `/configurar` | Inicia la configuración de la mascota: especie, sexo y peso. Con estos datos se calcula la meta diaria de hidratación. |
| `/status` | Muestra el estado actual del sistema: nivel del tanque, consumo del día, meta diaria, temperatura, TDS, detección de la mascota y estado de la bomba. |
| `/llamar` | Reproduce el audio configurado para llamar a la mascota y muestra los datos actuales del bebedero. |
| `/recordatorio HH MM` | Configura la hora del reporte diario de hidratación. Ejemplo: `/recordatorio 20 30`. |
| `/dashboards` | Muestra los enlaces al dashboard local y al panel para cargar un audio personalizado. |
| `/cambiarvoz` | Tiene la misma función que `/dashboards` y permite acceder al panel web para cargar una nueva voz. |
| `/cambioAgua` | Activa el modo de mantenimiento. Apaga la bomba y pausa temporalmente el registro de consumo y peso. |
| `/finCambio` | Finaliza el modo de cambio de agua y vuelve a estabilizar la lectura de la balanza. |
| `/setBalanzaCero` | Realiza la tara de la celda de carga y guarda el nuevo valor de cero. |
| `/calibrar` | Inicia la calibración de la balanza utilizando un volumen conocido de agua. |
| `/resetwifi` | Borra las redes Wi-Fi almacenadas y reinicia VitalPet en modo de configuración AP. |
| `/resetconsumo` | Reinicia el contador de consumo diario a 0 mL. Se utiliza principalmente para pruebas o demostraciones. |

### Configuración inicial de la mascota

Al enviar:

`/configurar`

el bot solicita:

1. Especie: **Perro** o **Gato**.
2. Sexo: **Macho** o **Hembra**.
3. Peso corporal en kilogramos.

Con esta información VitalPet calcula y almacena una meta diaria de hidratación.

### Calibración de la balanza

Para realizar una calibración completa:

1. Vacía el recipiente.
2. Envía `/setBalanzaCero`.
3. Añade un volumen conocido de agua, por ejemplo 500 mL.
4. Envía `/calibrar`.
5. Cuando el bot lo solicite, escribe únicamente el volumen utilizado, por ejemplo:

`500`

El nuevo factor de calibración se guarda en la memoria del dispositivo.

### Cambio de agua

Antes de retirar el depósito utiliza:

`/cambioAgua`

Esto apaga la bomba y pausa temporalmente las mediciones relacionadas con el consumo.

Después de lavar, rellenar y volver a colocar el recipiente, envía:

`/finCambio`

VitalPet espera a que el peso se estabilice antes de continuar con el monitoreo normal.

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
VitalPet/
│
├── README.md
├── .gitignore
│
├── src/
│   └── main.cpp
│
└── include/
    ├── apwifieeprommode.h
    ├── audio_memoria.h
    └── secrets.h.example
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
