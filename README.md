# Simple Chat Protocol

Sistema de chat cliente-servidor en C++ para Linux usando sockets TCP, multithreading, Protocol Buffers e interfaz gráfica Qt Widgets.

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
├── CMakeLists.txt                    # Build system
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
| CMake | 3.14+ |
| g++ / clang++ | C++17 |
| Protocol Buffers | libprotobuf-dev + protoc |
| Qt Widgets | Qt6 (preferido) o Qt5 |
| pthreads | incluido en Linux |

### Instalación en Ubuntu/Debian

```bash
# Compilador y build tools
sudo apt update
sudo apt install -y build-essential cmake

# Protocol Buffers
sudo apt install -y libprotobuf-dev protobuf-compiler

# Qt6 Widgets (Ubuntu 22.04+)
sudo apt install -y qt6-base-dev

# O Qt5 Widgets (Ubuntu 20.04 / sistemas más antiguos)
# sudo apt install -y qtbase5-dev
```

## Compilación

```bash
# Desde la raíz del proyecto
cmake -S . -B build
cmake --build build -j$(nproc)
```

Esto genera dos ejecutables en `build/`:
- `chat_server`
- `chat_client`

## Ejecución

### Servidor

```bash
# Básico
./build/chat_server <puerto>

# Con timeout personalizado (por defecto 180 segundos)
./build/chat_server 50000 --idle-timeout 60
```

El servidor escucha en todas las interfaces (`0.0.0.0`), lo que permite conexiones desde cualquier máquina en la LAN.

### Cliente

```bash
./build/chat_client <username> <IP_servidor> <puerto_servidor>

# Ejemplo: conexión local
./build/chat_client alice 127.0.0.1 50000

# Ejemplo: conexión en LAN
./build/chat_client bob 192.168.1.100 50000
```

## Interfaz Gráfica del Cliente

La UI usa Qt Widgets con tema oscuro (Catppuccin-inspired):

- **Barra superior**: usuario actual, selector de estado, botones de acción
- **Panel izquierdo**: lista de usuarios conectados con indicador de estado
- **Panel central**: pestañas de chat
  - 💬 **General**: chat broadcast
  - 📩 **DM tabs**: una pestaña por conversación directa (se crean al recibir/enviar)
  - 🖥️ **Sistema**: log de mensajes del servidor y eventos
- **Panel inferior**: input de mensaje, selector General/DM, combo de destinatario

### Acciones disponibles
| Acción | Cómo |
|---|---|
| Chat general | Modo "General" + escribir + Enviar (o Enter) |
| Mensaje directo | Modo "DM" + seleccionar destinatario + escribir + Enviar |
| Iniciar DM rápido | Doble clic en usuario de la lista |
| Cambiar estado | Selector de estado en toolbar |
| Refrescar usuarios | Botón 🔄 Usuarios |
| Info de usuario | Botón ℹ️ Info Usuario |
| Ayuda | Botón ❓ Ayuda |
| Salir | Cerrar ventana (envía Quit al servidor) |

## Mapeo de Status

| Proto (`StatusEnum`) | UI Display | Descripción |
|---|---|---|
| `ACTIVE` (0) | 🟢 ACTIVO | Disponible |
| `DO_NOT_DISTURB` (1) | 🔴 OCUPADO | No molestar |
| `INVISIBLE` (2) | ⚫ INACTIVO | Invisible/ausente |

## Protocolo de Comunicación

### Framing TCP

Cada mensaje sobre TCP lleva un header de 5 bytes:

```
┌──────────────────┬───────────────────────────┬────────────────────────────┐
│  1 byte: type    │  4 bytes: payload length   │  N bytes: protobuf payload │
│  (uint8)         │  (uint32, big-endian)       │                            │
└──────────────────┴───────────────────────────┴────────────────────────────┘
```

### Tipos de Mensaje

| Type ID | Dirección | Proto | Descripción |
|---|---|---|---|
| 1 | client → server | `Register` | Registro de usuario |
| 2 | client → server | `MessageGeneral` | Mensaje broadcast |
| 3 | client → server | `MessageDM` | Mensaje directo |
| 4 | client → server | `ChangeStatus` | Cambio de estado |
| 5 | client → server | `ListUsers` | Solicitar lista de usuarios |
| 6 | client → server | `GetUserInfo` | Info de un usuario |
| 7 | client → server | `Quit` | Desconexión |
| 10 | server → client | `ServerResponse` | Respuesta genérica |
| 11 | server → client | `AllUsers` | Lista de usuarios |
| 12 | server → client | `ForDm` | DM reenviado |
| 13 | server → client | `BroadcastDelivery` | Broadcast reenviado |
| 14 | server → client | `GetUserInfoResponse` | Info de usuario |

## Convención de `ForDm.username_des`

El campo `username_des` del proto `ForDm` se usa de la siguiente manera:

- Cuando el servidor reenvía un DM al destinatario, coloca en `username_des` el **username del remitente** (no del destinatario).
- El cliente receptor interpreta `username_des` como "quién me envió este DM".
- Esto permite que el cliente muestre correctamente el nombre del remitente sin modificar el proto.

## Códigos de ServerResponse

| Código | Significado | `is_successful` |
|---|---|---|
| 200 | Registro exitoso | true |
| 201 | Estado actualizado | true |
| 202 | DM enviado | true |
| 203 | Quit confirmado | true |
| 400 | Solicitud inválida | false |
| 401 | No registrado / error de sesión | false |
| 404 | Usuario destino no encontrado | false |
| 409 | Username ya existe | false |
| 410 | IP ya registrada | false |
| 500 | Error interno del servidor | false |
| 602 | Marcado INACTIVO por timeout automático | true |
| 603 | Estado restaurado tras actividad | true |

## Notas de Interoperabilidad

1. **Protocolo fijo**: El framing TCP (5 bytes) y los IDs de mensaje son los acordados por la clase. Cualquier implementación que siga el mismo protocolo puede interoperar.

2. **Identity validation**: El servidor usa la IP real del peer (`getpeername`) y el username de la sesión registrada. No confía en los campos `username`/`ip` enviados por el cliente en cada mensaje, salvo para logging.

3. **Registro**: Se valida unicidad tanto de username como de IP. Esto implica que **no se pueden ejecutar dos clientes desde la misma máquina** (misma IP) simultáneamente hacia el mismo servidor.

4. **Orden de `AllUsers`**: Los usuarios se devuelven ordenados alfabéticamente.

5. **Broadcast echo**: El emisor recibe su propio `BroadcastDelivery`. El cliente no muestra eco local adicional para evitar duplicados.

6. **DM echo**: El cliente muestra eco local del DM enviado inmediatamente. El servidor envía un `ServerResponse(202)` de confirmación.

7. **MSG_NOSIGNAL**: Se usa en `send()` para evitar SIGPIPE en conexiones cerradas.

## Decisiones de UX

| Decisión | Elección |
|---|---|
| ¿Enviar mensajes en OCUPADO? | Sí, puede enviar |
| ¿Enviar mensajes en INACTIVO? | Sí, puede enviar |
| Auto-INACTIVO | Después de 180s (configurable) sin actividad, el servidor marca INVISIBLE |
| Restaurar status | Al hacer cualquier acción real (enviar mensaje, cambiar status, listar, etc.) se restaura a ACTIVE |
| Refrescos automáticos de UI | NO cuentan como actividad real |
| Eco de broadcast | Viene del servidor (no eco local) |
| Eco de DM | Eco local inmediato + confirmación del servidor |

## Ejemplo de Uso (LAN)

```bash
# Terminal 1: Servidor
./build/chat_server 50000 --idle-timeout 60

# Terminal 2: Cliente Alice (máquina A)
./build/chat_client alice 192.168.1.100 50000

# Terminal 3: Cliente Bob (máquina B)
./build/chat_client bob 192.168.1.100 50000
```

1. Alice y Bob ven la ventana del chat
2. Alice escribe "Hola a todos" en modo General → aparece en el chat de ambos
3. Bob hace doble clic en "alice" → se abre tab de DM → escribe "Hola Alice" → llega solo a Alice
4. Alice cambia estado a OCUPADO → la lista de usuarios de Bob se actualiza
5. Después de 60s sin actividad, el servidor auto-marca INACTIVO al usuario
6. Al cerrar la ventana, se envía Quit y la sesión se limpia

## Limitaciones Conocidas

1. **Una IP = un cliente**: Por diseño del protocolo, no pueden haber dos clientes desde la misma IP. Para pruebas locales, se pueden usar interfaces de red virtuales.
2. **IPv4 únicamente**: El sistema usa `AF_INET` (IPv4).
3. **Sin persistencia**: Los mensajes no se guardan en disco; al desconectar se pierden.
4. **Sin cifrado**: No hay TLS/SSL. La comunicación es en texto plano.
5. **Sin reconnexión automática**: Si se pierde la conexión, hay que reiniciar el cliente.
