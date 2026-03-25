# Simple Chat Protocol

Sistema de chat cliente-servidor en C++ para macOS y Linux usando sockets TCP, multithreading, Protocol Buffers e interfaz gráfica Qt Widgets.

## Arquitectura General

```
┌──────────────────┐       TCP + Protobuf        ┌──────────────────┐
│   chat_client    │ ◄──── framing 5 bytes ────► │   chat_server    │
│  (Qt Widgets UI) │                              │  (multithreaded) │
└──────────────────┘                              └──────────────────┘
      │                                                  │
      ├─ NetworkThread (QThread)                         ├─ Accept loop (main thread)
      ├─ NetworkSender (mutex-protected)                 ├─ Client handler threads
      └─ MainWindow (Qt event loop)                      ├─ Session registry (mutex)
                                                         └─ Watchdog thread (inactivity)
```

## Estructura del Proyecto

```
.
├── CMakeLists.txt                    # Build system (modo CONFIG de Protobuf)
├── include/chat/
│   ├── protocol.h                    # Framing TCP, constantes, helpers
│   └── server.h                      # Declaración ChatServer
├── src/
│   ├── server/
│   │   ├── main.cpp                  # Entry point del servidor
│   │   └── server.cpp                # Implementación completa del servidor
│   └── client/
│       ├── main.cpp                  # Entry point del cliente
│       ├── mainwindow.h / .cpp       # UI Qt Widgets
│       ├── network.h / .cpp          # Hilo de red y sender
├── protos/
│   ├── common.proto                  # StatusEnum compartido
│   ├── cliente-side/                 # Mensajes Cliente → Servidor
│   └── server-side/                  # Mensajes Servidor → Cliente
├── docs/
│   ├── instructions.md              # Requisitos del proyecto
│   ├── protocol_standard.md         # Especificación del protocolo
│   └── IMPLEMENTATION.md            # Decisiones de implementación
└── README.md
```

## Dependencias

| Dependencia | Versión mínima |
|---|---|
| CMake | 3.20+ (Recomendado) |
| g++ / AppleClang | C++17 |
| Protocol Buffers | libprotobuf + protoc + abseil |
| Qt Widgets | Qt6 |
| pthreads | incluido en Linux/macOS nativamente |

### Instalación en macOS (Recomendada)
Para correr de manera nativa en Mac (Intel o Apple Silicon), instala las dependencias mediante [Homebrew](https://brew.sh/):
```bash
brew install cmake protobuf qt
```

### Instalación en Ubuntu/Debian
```bash
sudo apt update
sudo apt install -y build-essential cmake libprotobuf-dev protobuf-compiler qt6-base-dev
```

## Compilación

```bash
# Desde la raíz del proyecto
mkdir build
cd build
cmake ..
make -j4
```

Esto genera dos ejecutables en la carpeta `build/`:
- `chat_server`
- `chat_client`

## Ejecución y Pruebas en Red (macOS / LAN RJ45)

El código del servidor hace uso de `INADDR_ANY` (`0.0.0.0`), por lo que **escucha en todas las interfaces de red simultáneamente**, siendo ideal para pruebas con un switch mediante cable RJ45.

### Servidor

```bash
cd build/
# Básico (escucha en el puerto indicado)
./chat_server 8080

# Con timeout personalizado (por defecto 180 segundos)
./chat_server 8080 --idle-timeout 60
```
> **Nota de Firewall en macOS:** La primera vez que ejecutes el servidor, macOS lanzará una alerta preguntando si deseas que la aplicación acepte conexiones entrantes. **Asegúrate de darle clic en Permitir**. De lo contrario, dirígete a `Configuración del Sistema > Red > Firewall > Opciones` y agrégalo manualmente.

### Encontrar tu IP para la evaluación (RJ45)
Si tu equipo hace de servidor y están todos conectados por un switch:
1. Ve a **Configuración del Sistema > Red > Ethernet**.
2. Anota tu dirección IP (ya sea manual, pej: `192.168.1.10`, o automática APIPA, pej: `169.254.x.x`). 
3. Pásales esa IP a tus compañeros para que se conecten.

### Cliente

```bash
cd build/
./chat_client <username> <IP_servidor> <puerto_servidor>

# Ejemplo: conexión local
./chat_client alice 127.0.0.1 8080

# Ejemplo: conexión en LAN a otro compañero
./chat_client bob 192.168.1.10 8080
```

## Interfaz Gráfica del Cliente

La UI usa Qt Widgets con un diseño limpio:

- **Barra superior**: usuario actual, selector de estado, botones de acción.
- **Panel izquierdo**: lista de usuarios conectados con indicador visual de estado.
- **Panel central**: pestañas de chat.
  - 💬 **General**: chat broadcast.
  - 📩 **DM tabs**: una pestaña por conversación directa (se crean automáticamente al enviar/recibir DMs).
  - 🖥️ **Sistema**: log de servidor.
- **Panel inferior**: input de texto, envío, y selector de usuario.

### Acciones disponibles
| Acción | Cómo |
|---|---|
| Chat general | Modo "General" + escribir + Enviar (o Enter) |
| Mensaje directo | Modo "DM" + seleccionar destinatario + escribir + Enviar |
| Iniciar DM rápido | Doble clic en usuario de la lista |
| Cambiar estado | Selector de estado en toolbar |
| Ayuda/Info | Botones en la barra superior |

## Mapeo de Status

| Proto (`StatusEnum`) | UI Display | Descripción |
|---|---|---|
| `ACTIVE` (0) | 🟢 ACTIVO | Disponible |
| `DO_NOT_DISTURB` (1) | 🔴 OCUPADO | No molestar |
| `INVISIBLE` (2) | ⚫ INACTIVO | Invisible/ausente |

## Protocolo de Comunicación

### Framing TCP

Cada mensaje sobre TCP lleva un header fijo de 5 bytes que precede al buffer binario del mensaje de Protobuf:

```
┌──────────────────┬───────────────────────────┬────────────────────────────┐
│  1 byte: type    │  4 bytes: payload length   │  N bytes: protobuf payload │
│  (uint8)         │  (uint32, big-endian)       │                            │
└──────────────────┴───────────────────────────┴────────────────────────────┘
```

### Códigos de Respuesta del Servidor (ServerResponse)

| Código | Significado | `is_successful` |
|---|---|---|
| 200/201/202/203 | Éxitos (Registro, Estado, DM, Quit) | true |
| 400 | Solicitud inválida / Mensaje corrupto | false |
| 401 | No registrado / error de sesión | false |
| 404 | Usuario destino no encontrado | false |
| 409 | Username ya existe | false |
| 410 | IP ya registrada | false |
| 602 | Marcado INACTIVO por timeout automático del server | true |
| 603 | Estado restaurado tras actividad posterior | true |

## Limitaciones Conocidas

1. **Una IP = un cliente**: Por diseño del protocolo, no pueden haber dos usuarios operando un cliente desde la misma IP. Para pruebas en una sola máquina física se debe evitar usar la misma IP de loopback o lanzar varios clientes sin que este rebote.
2. **IPv4 únicamente**: El sistema usa `AF_INET` tradicional (IPv4).
3. **Sin persistencia/cifrado**: Los mensajes se manejan en texto plano sobre la memoria volátil del servidor. Al apagarse, todo el historial se descarta. No cuenta con SSL/TLS.
