# CrossPoint Mixtli

Firmware para el lector **Xteink X4 / X3** basado en [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) **1.6.5**, con:

- **Control Bluetooth para pasar página**: controles BLE de página, teclados y **mouse Bluetooth** (clic izquierdo/derecho/central, botones extra y rueda). Basado en la rama `feat-ble` de CrossPoint, adaptada a 1.6.5.
- **Reconexión automática** al control/mouse emparejado (también con mouse que rotan su dirección privada).
- **Recibir libros por Bluetooth** desde un teléfono Android con la app [Mixtli Libros](https://github.com/cxrlind7/mixtli-libros), sin WiFi ni cables.
- **Bluetooth en la pantalla de inicio**: *Receive via Bluetooth* (en lugar de *File Transfer*) y un interruptor *Bluetooth: On/Off*.
- **Pantalla de arranque** con una nube y el texto "Mixtli".
- Textos del Bluetooth en español.

> ⚠️ Firmware no oficial, sin garantías. Úsalo bajo tu propio riesgo. Solo para **X4 / X3** (chip ESP32-C3); no lo instales en X4 Pro / X4C.

## Usar el Bluetooth

1. Abre **Ajustes → Controles → Bluetooth**, actívalo y elige **Buscar y emparejar**.
2. Pon tu control o mouse en modo emparejamiento y selecciónalo.
3. En **Asignar botones del control** elige *Página siguiente* / *Página anterior* y pulsa el botón (o gira la rueda) que quieras usar.

✅ Probado en un X4 con un **mouse Bluetooth Logitech**: clics, rueda y reconexión automática funcionando.

Para ahorrar memoria el Bluetooth solo está encendido **dentro de un libro** y en la pantalla de Bluetooth. Si el mouse está dormido, muévelo o haz clic para que se reconecte.

Si emparejaste el mouse con una versión anterior de este firmware, **olvídalo** (mantén pulsado sobre él en *Dispositivos emparejados*) y vuelve a emparejarlo.

El interruptor **Bluetooth: On/Off** de la pantalla de inicio enciende o apaga este control de página sin entrar a Ajustes.

## Recibir libros desde el teléfono

Se usa junto con la app Android **[Mixtli Libros](https://github.com/cxrlind7/mixtli-libros)** (descarga el APK en sus *Releases*).

1. En el Xteink, en la pantalla de inicio, abre **Receive via Bluetooth** (*Recibir por Bluetooth* en español). Muestra *Ready to receive books* y el nombre **Xteink Mixtli**.
2. En el teléfono abre **Mixtli Libros** → **Agregar** y elige uno o varios EPUB, o desde cualquier app usa **Compartir → Mixtli Libros**.
3. El lector muestra el progreso y, al terminar, *Book saved*. El libro queda en `/Mis Libros Kindle` (o en la raíz de la SD si esa carpeta no existe) y aparece en la biblioteca.

Detalles:

- No hace falta emparejar: la app busca al lector por su servicio Bluetooth.
- Cada archivo se verifica con CRC32; si llega dañado se descarta y la app lo marca para reintentar.
- Si ya existe un libro con el mismo nombre se guarda como `Nombre (2).epub`.
- Mientras esta pantalla está abierta, el control/mouse Bluetooth se desconecta (el radio lo usa la transferencia) y se reconecta al abrir un libro.
- Velocidad aproximada: un EPUB típico (300 KB – 2 MB) tarda de unos segundos a un par de minutos.

### Protocolo (para quien quiera hacer otro cliente)

Servicio BLE `8f2c1000-6b1e-4d3a-9b7e-4d6978746c69`:

| Característica | UUID | Uso |
| --- | --- | --- |
| CTRL | `8f2c1001-…-4d6978746c69` | write + notify. Teléfono → `B\|<tamaño>\|<crc32 hex>\|<nombre>`, `E` (fin), `C` (cancelar). Lector → `R` (listo), `A\|<bytes guardados>` (cada 4 KB), `OK\|<nombre guardado>` o `ER\|<motivo>` |
| DATA | `8f2c1002-…-4d6978746c69` | write without response: los bytes del archivo, en orden |

El cliente no debe tener más de 8 KB enviados sin confirmar (el lector usa un búfer de 16 KB). El código está en [`src/activities/network/BleReceiveActivity.cpp`](src/activities/network/BleReceiveActivity.cpp).

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

**Si la página falla** (por ejemplo *"No se pudo obtener el módulo importado dinámicamente … esptool.bundle"*), instálalo con `esptool` desde la terminal. Con esto se conservan los ajustes y los dispositivos Bluetooth emparejados (cambia `COM4` por tu puerto; en Linux/macOS será algo como `/dev/ttyACM0`):

```bash
python -m esptool --chip esp32c3 --port COM4 --baud 921600 --after no-reset write-flash 0x10000 firmware.bin
python -m esptool --chip esp32c3 --port COM4 erase-region 0xe000 0x2000
```

El segundo comando hace que el lector arranque desde el firmware recién escrito.

**Desde la tarjeta SD:** copia el `.bin` a la SD y en el lector ve a **Settings → System → SD Card Firmware Update**.

## Créditos y licencia

Basado en [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) y el [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (incluido en `freeink-sdk/`), bajo licencia MIT (ver `LICENSE`). La guía de uso original está en [`USER_GUIDE.md`](USER_GUIDE.md).
