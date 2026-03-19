# Guía Completa: Compilar y Ejecutar en Ubuntu 22.04 (VirtualBox)

Guía paso a paso para configurar una VM Ubuntu 22.04 en VirtualBox (MacBook Pro 2019), clonar el repositorio, compilar y ejecutar el sistema de chat.

---

## Paso 0 — Subir los cambios al repositorio (en tu Mac)

Antes de clonar en la VM, **los archivos nuevos deben estar en GitHub**. Abre Terminal en tu Mac y ejecuta:

```bash
cd ~/Documents/Simple-Chat-Protocol

# Verificar que los archivos nuevos existen
ls src/ include/ CMakeLists.txt

# Agregar todos los archivos nuevos
git add -A

# Commit
git commit -m "Implementación completa: servidor, cliente Qt, CMake, docs"

# Push
git push origin main
```

Si `git push` pide credenciales y falla, necesitas configurar un **Personal Access Token (PAT)** de GitHub:
1. Ve a https://github.com/settings/tokens
2. Genera un token con permiso `repo`
3. Usa el token como contraseña cuando Git lo pida

---

## Paso 1 — Configurar la VM en VirtualBox

### 1.1 Verificar configuración de red

Esto es **crítico** para que la VM tenga internet y pueda clonar el repo.

1. Apaga la VM si está encendida
2. En VirtualBox, selecciona la VM → **Settings** (⚙️) → **Network**
3. En **Adapter 1**:
   - ✅ **Enable Network Adapter** debe estar marcado
   - **Attached to**: selecciona **NAT** (para tener internet)
   - Haz clic en **OK**
4. Si además quieres probar el chat entre tu Mac y la VM, agrega un segundo adaptador:
   - Ve a **Adapter 2**
   - ✅ marca **Enable Network Adapter**
   - **Attached to**: **Bridged Adapter**
   - **Name**: selecciona tu adaptador WiFi (generalmente `en0` o el nombre de tu WiFi)
   - Haz clic en **OK**

> **NAT** = la VM puede acceder a internet pero está aislada de tu LAN.
> **Bridged** = la VM obtiene una IP en tu red local (permite chat entre Mac y VM).

### 1.2 Asignar recursos suficientes

1. **Settings** → **System** → **Motherboard**: mínimo **2048 MB** de RAM (recomiendo 4096)
2. **Settings** → **System** → **Processor**: mínimo **2 CPUs**
3. **Settings** → **Display** → **Video Memory**: **128 MB** (necesario para Qt GUI)
4. **Settings** → **Display** → Asegúrate que **Graphics Controller** esté en **VMSVGA**

### 1.3 Instalar Guest Additions (si no están instaladas)

Esto mejora la resolución de pantalla y permite copiar/pegar entre Mac y VM.

1. Enciende la VM
2. En el menú de VirtualBox (arriba): **Devices** → **Insert Guest Additions CD image...**
3. Dentro de Ubuntu, abre Terminal y ejecuta:

```bash
sudo apt update
sudo apt install -y build-essential dkms linux-headers-$(uname -r)
sudo mount /dev/cdrom /mnt
sudo /mnt/VBoxLinuxAdditions.run
sudo reboot
```

---

## Paso 2 — Preparar Ubuntu (dentro de la VM)

Enciende la VM e inicia sesión. Abre una **Terminal** (Ctrl+Alt+T).

### 2.1 Actualizar el sistema

```bash
sudo apt update && sudo apt upgrade -y
```

### 2.2 Instalar herramientas de desarrollo

```bash
sudo apt install -y build-essential cmake git
```

### 2.3 Instalar Protocol Buffers

```bash
sudo apt install -y libprotobuf-dev protobuf-compiler
```

Verifica la instalación:

```bash
protoc --version
# Debe mostrar algo como: libprotoc 3.12.x o superior
```

### 2.4 Instalar Qt6

Ubuntu 22.04 incluye Qt6 en sus repositorios:

```bash
sudo apt install -y qt6-base-dev
```

Verifica:

```bash
dpkg -l | grep qt6-base-dev
# Debe mostrar una línea como: ii qt6-base-dev ...
```

> **Si por alguna razón Qt6 no se instala**, puedes usar Qt5 como alternativa:
> ```bash
> sudo apt install -y qtbase5-dev
> ```
> El CMakeLists.txt del proyecto soporta ambas versiones automáticamente.

---

## Paso 3 — Clonar el repositorio

```bash
cd ~
git clone https://github.com/iancumes/Simple-Chat-Protocol.git
cd Simple-Chat-Protocol
```

Si el repositorio es **privado**, Git te pedirá credenciales:
- **Username**: tu usuario de GitHub
- **Password**: tu **Personal Access Token** (NO tu contraseña de GitHub)

Verifica que los archivos del proyecto están presentes:

```bash
ls src/ include/ CMakeLists.txt
# Debería mostrar: client  server   (en src/)
#                  chat             (en include/)
#                  CMakeLists.txt
```

---

## Paso 4 — Compilar el proyecto

```bash
cd ~/Simple-Chat-Protocol

# Configurar el build
cmake -S . -B build

# Compilar (usa todos los cores disponibles)
cmake --build build -j$(nproc)
```

### Posibles errores y soluciones

| Error | Solución |
|---|---|
| `Could NOT find Protobuf` | `sudo apt install libprotobuf-dev protobuf-compiler` |
| `Could not find a package... Qt6` y `Qt5` | `sudo apt install qt6-base-dev` (o `qtbase5-dev`) |
| `protoc: command not found` | `sudo apt install protobuf-compiler` |
| `No CMAKE_CXX_COMPILER could be found` | `sudo apt install build-essential` |

Si todo va bien, verás al final algo como:

```
[100%] Built target chat_client
```

Y los ejecutables quedan en `build/`:

```bash
ls build/chat_server build/chat_client
```

---

## Paso 5 — Ejecutar el servidor

```bash
# Con timeout por defecto (180 segundos)
./build/chat_server 50000

# O con timeout corto para pruebas rápidas (30 segundos)
./build/chat_server 50000 --idle-timeout 30
```

Deberías ver:

```
[SERVER] Listening on port 50000 (idle timeout: 180s)
```

> **Deja esta terminal abierta.** El servidor corre en primer plano.

---

## Paso 6 — Ejecutar el cliente

Abre **otra terminal** (Ctrl+Alt+T) en la VM:

```bash
cd ~/Simple-Chat-Protocol

# Conectar al servidor local
./build/chat_client miusuario 127.0.0.1 50000
```

Debería abrirse una **ventana gráfica** con el chat.

### Si quieres un segundo cliente (en la misma VM)

Debido a la restricción de IP única, no puedes correr dos clientes desde la misma IP. Pero para probar localmente, puedes crear una interfaz virtual:

```bash
# Crear interfaz virtual con otra IP (requiere sudo)
sudo ip addr add 127.0.0.2/8 dev lo

# Ahora puedes correr el segundo cliente con esa IP
./build/chat_client otrousuario 127.0.0.2 50000
```

> Nota: El servidor detectará 127.0.0.2 como IP diferente de 127.0.0.1.

---

## Paso 7 — Probar entre la VM y otra máquina (LAN)

Para probar con otro compañero o entre tu Mac y la VM:

### 7.1 Averiguar la IP de la VM

```bash
ip addr show
# Busca la IP del adaptador bridged (generalmente algo como 192.168.x.x)
# El adaptador se llama enp0s8 o similar
```

### 7.2 Abrir el firewall (si está activo)

```bash
# Verificar si el firewall está activo
sudo ufw status

# Si está activo, abrir el puerto del chat
sudo ufw allow 50000/tcp
```

### 7.3 Ejecutar el servidor en la VM

```bash
./build/chat_server 50000
```

### 7.4 Conectar clientes desde otras máquinas

Desde cualquier máquina en la misma red:

```bash
./build/chat_client alice <IP_DE_LA_VM> 50000
```

---

## Paso 8 — Verificar funcionalidad

Con el servidor corriendo y al menos un cliente conectado:

1. **Chat general**: escribe un mensaje y presiona Enter o "Enviar"
2. **Lista de usuarios**: clic en 🔄 Usuarios
3. **Cambiar estado**: usa el selector en la barra superior
4. **DM**: doble clic en un usuario de la lista, o cambia el modo a "DM"
5. **Info de usuario**: clic en ℹ️ Info Usuario
6. **Ayuda**: clic en ❓ Ayuda
7. **Salir**: cierra la ventana (envía Quit al servidor)

---

## Resumen de comandos (copiar y pegar)

```bash
# === EN TU MAC (Terminal): subir cambios ===
cd ~/Documents/Simple-Chat-Protocol
git add -A
git commit -m "Implementación completa"
git push origin main

# === EN LA VM (Terminal): setup completo ===
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential cmake git libprotobuf-dev protobuf-compiler qt6-base-dev
cd ~
git clone https://github.com/iancumes/Simple-Chat-Protocol.git
cd Simple-Chat-Protocol
cmake -S . -B build
cmake --build build -j$(nproc)

# === Ejecutar ===
./build/chat_server 50000                       # Terminal 1
./build/chat_client miusuario 127.0.0.1 50000   # Terminal 2
```

---

## Problemas comunes

### "No puedo ver la ventana del cliente"
- Asegurar que VirtualBox tenga **128 MB de Video Memory** y **VMSVGA**
- Verificar que Guest Additions estén instaladas
- Probar reiniciar la VM

### "cmake no encuentra Qt"
```bash
# Buscar qué paquete de Qt hay disponible
apt list --installed 2>/dev/null | grep qt
# Si no hay nada, instalar:
sudo apt install -y qt6-base-dev
```

### "git clone pide contraseña y falla"
- El repo puede ser privado. Usa un Personal Access Token de GitHub como contraseña.
- O cambia el repo a público temporalmente.

### "El servidor dice 'Address already in use'"
```bash
# Matar procesos anteriores del servidor
pkill chat_server
# O usar otro puerto
./build/chat_server 50001
```
