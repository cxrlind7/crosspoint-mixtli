# CrossPoint Mixtli

Firmware para el lector **Xteink X4 / X3** basado en [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) **1.6.5**, con:

- **Control Bluetooth para pasar página**: controles BLE de página, teclados y **mouse Bluetooth** (clic izquierdo/derecho/central, botones extra y rueda). Basado en la rama `feat-ble` de CrossPoint, adaptada a 1.6.5.
- **Reconexión automática** al control/mouse emparejado (también con mouse que rotan su dirección privada).
- **Pantalla de arranque** con una nube y el texto "Mixtli".
- Textos del Bluetooth en español.

> ⚠️ Firmware no oficial, sin garantías. Úsalo bajo tu propio riesgo. Solo para **X4 / X3** (chip ESP32-C3); no lo instales en X4 Pro / X4C.

## Usar el Bluetooth

1. Abre **Ajustes → Controles → Bluetooth**, actívalo y elige **Buscar y emparejar**.
2. Pon tu control o mouse en modo emparejamiento y selecciónalo.
3. En **Asignar botones del control** elige *Página siguiente* / *Página anterior* y pulsa el botón (o gira la rueda) que quieras usar.

Para ahorrar memoria el Bluetooth solo está encendido **dentro de un libro** y en la pantalla de Bluetooth. Si el mouse está dormido, muévelo o haz clic para que se reconecte.

Si emparejaste el mouse con una versión anterior de este firmware, **olvídalo** (mantén pulsado sobre él en *Dispositivos emparejados*) y vuelve a emparejarlo.

## Compilar

### Requisitos

- [Python 3.10+](https://www.python.org/downloads/)
- [Git](https://git-scm.com/downloads)
- ~5 GB libres (herramientas de ESP32)

### 1. Descargar el código

```bash
git clone --recursive https://github.com/cxrlind7/crosspoint-mixtli.git
cd crosspoint-mixtli
```

Si ya lo clonaste sin `--recursive`: `git submodule update --init --recursive`

### 2. Instalar PlatformIO (versión fija 6.1.19)

La 6.2.0 rompe la compilación (error `No module named 'SCons.Tool.FortranCommon'`), así que usa exactamente esta:

```bash
pip install https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip
```

### 3. Compilar

**Linux / macOS**

```bash
pio run -e gh_release
```

**Windows (PowerShell)** — usa una carpeta corta para las herramientas, porque Windows limita la longitud de las rutas:

```powershell
$env:PLATFORMIO_CORE_DIR = 'C:\pio'
python -m platformio run -e gh_release
```

La primera compilación descarga varios GB y recompila el núcleo de Arduino: puede tardar **30–60 min**. Las siguientes, unos 5 min.

Si en el primer intento falla con `No module named 'SCons...'`, fija también la versión dentro del entorno de PlatformIO y repite el paso 3:

```bash
# Linux / macOS
~/.platformio/penv/bin/python -m pip install "pioarduino==6.1.19"
# Windows
C:\pio\penv\Scripts\python.exe -m pip install "pioarduino==6.1.19"
```

El firmware queda en `.pio/build/gh_release/firmware.bin`.

## Instalar en el lector

1. Conecta el X4 a la computadora con un cable USB-C de datos.
2. Abre <https://crosspointreader.com/#flash-tools> en Chrome o Edge.
3. Elige **X4**, pulsa **Custom .bin** y sube `firmware.bin`.

Para volver al firmware original, usa la misma página.

## Créditos y licencia

Basado en [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) y el [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (incluido en `freeink-sdk/`), bajo licencia MIT (ver `LICENSE`). La guía de uso original está en [`USER_GUIDE.md`](USER_GUIDE.md).
