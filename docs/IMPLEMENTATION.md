# Decisiones de Implementación

Documento técnico detallado sobre las decisiones tomadas en la implementación del sistema de chat.

## 1. Modelo de Concurrencia del Servidor

**Decisión**: Un hilo por cliente + hilo watchdog.

- **Hilo principal**: ejecuta `accept()` en loop.
- **Hilos de cliente**: cada conexión aceptada lanza un `std::thread` que ejecuta `handle_client()` con un read loop.
- **Hilo watchdog**: revisa cada 10 segundos si algún usuario excedió el timeout de inactividad.
- **Protección de datos**: `std::mutex` global para el registro de sesiones, `std::mutex` por sesión para serializar escrituras al socket.

**Justificación**: Es el modelo más simple y robusto para el caso de uso (decenas de clientes, no miles). Evita complejidad de epoll/select para un proyecto académico donde la claridad importa.

## 2. Validación de Identidad

**Decisión**: El servidor nunca confía en los campos `username` o `ip` enviados por el cliente en mensajes posteriores al registro.

- Al registrarse, se usa `getpeername()` para obtener la IP real.
- En todos los mensajes subsiguientes, la identidad del cliente se determina por el `socket_fd` mapeado a la sesión.
- Los campos `username_origin`, `username`, `ip` en mensajes como `MessageGeneral`, `ChangeStatus`, etc. se ignoran o se usan solo para logging.

**Justificación**: Seguridad básica. Un cliente malicioso no puede suplantar a otro.

## 3. Convención de `ForDm.username_des`

**Problema**: El proto `ForDm` tiene campos `username_des` y `message`. No hay campo para el remitente.

**Solución**: Cuando el servidor reenvía un DM al destinatario, coloca el **username del remitente** en `username_des`.

- El cliente receptor interpreta `ForDm.username_des` como "quién me envió este mensaje".
- Esto es semánticamente inconsistente con el nombre del campo, pero permite la funcionalidad sin modificar el proto.
- Documentado explícitamente en README y aquí.

## 4. Manejo de Inactividad

**Definición de "actividad relevante"**: Solo cuentan acciones explícitas del usuario:
- Enviar mensaje general (`MessageGeneral`)
- Enviar DM (`MessageDM`)
- Cambiar estado (`ChangeStatus`)
- Solicitar lista de usuarios (`ListUsers`)
- Solicitar info de usuario (`GetUserInfo`)

**NO cuentan como actividad**:
- Refrescos automáticos de la UI
- Mensajes recibidos del servidor (son recepción pasiva)

**Comportamiento al exceder timeout**:
1. El servidor marca `status = INVISIBLE` en la sesión.
2. El campo `manual_status` no se modifica (guarda el último estado elegido manualmente).
3. Se envía `ServerResponse` con código 602 al cliente notificando el cambio.

**Restauración**:
- Cuando el usuario auto-inactivo realiza cualquier acción real, el servidor:
  1. Restaura `status = ACTIVE`
  2. Actualiza `manual_status = ACTIVE`
  3. Envía `ServerResponse` con código 603
  4. Reinicia el timer de actividad

## 5. Echo de Mensajes

### Broadcast
- El emisor **sí recibe** su propio `BroadcastDelivery` del servidor.
- El cliente **no** muestra eco local adicional.
- Resultado: un solo mensaje en el chat general para el emisor.

### DM
- El cliente muestra **eco local inmediato** ("Tú → usuario: mensaje") en la pestaña de DM.
- El servidor envía `ServerResponse(202)` de confirmación o `ServerResponse(404)` si el destinatario no existe.
- El cliente no recibe un `ForDm` de su propio mensaje (solo lo recibe el destinatario).

## 6. Arquitectura del Cliente Qt

```
┌─────────────────────────────────────────────────────┐
│                    MainWindow                        │
│  ┌─────────────────────────────────────────────┐    │
│  │ Toolbar: [👤 user] [Estado ▼] [🔄] [ℹ️] [❓]│    │
│  ├─────────┬───────────────────────────────────┤    │
│  │ Usuarios│  TabWidget                         │    │
│  │  list   │  ┌─────────────────────────────┐  │    │
│  │ ┌─────┐ │  │ 💬 General │ 📩 DM1 │ ...   │  │    │
│  │ │alice│ │  │                              │  │    │
│  │ │bob  │ │  │  [chat history / read-only]  │  │    │
│  │ │...  │ │  │                              │  │    │
│  │ └─────┘ │  └─────────────────────────────┘  │    │
│  ├─────────┴───────────────────────────────────┤    │
│  │ [General/DM ▼] [Dest ▼] [__input___] [Enviar]│   │
│  └─────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────┘
```

**Threading**:
- **Hilo principal (Qt event loop)**: toda la UI y slots.
- **NetworkThread (QThread)**: read loop en `run()`, emite señales Qt.
- **Conexión**: `Qt::QueuedConnection` para que las señales se procesen en el hilo principal.
- **NetworkSender**: envío protegido por `std::mutex`, puede ser llamado desde el hilo principal sin bloquear la UI (los envíos son rápidos).

## 7. Deadlock Prevention

Orden de locking:
1. `sessions_mutex_` (global) siempre se adquiere primero.
2. `session->send_mutex` (per-socket) se adquiere segundo, dentro del lock global.
3. Nunca se adquiere `sessions_mutex_` mientras se tenga un `send_mutex`.

Caso especial: En `handle_register`, se envía la respuesta con `sessions_mutex_` ya adquirido. Se usa envío directo sin pasar por `send_to_client()` para evitar doble-lock.

## 8. Partial Reads / Writes

`read_exact()` y `write_exact()` implementan loops que manejan lecturas/escrituras parciales, comunes en TCP especialmente con payloads grandes o redes congestionadas. Ambas funciones retornan `false` en EOF o error, permitiendo limpieza inmediata.

## 9. Tolerancia a Fallos

| Fallo | Manejo |
|---|---|
| Protobuf malformado | `ServerResponse(400)` al cliente |
| Message type desconocido | `ServerResponse(400)` + log |
| Desconexión abrupta | `recv` devuelve 0/error → `remove_session()` |
| SIGPIPE | `MSG_NOSIGNAL` en `send()` |
| Socket cerrado durante write | `write_exact()` devuelve false → handler termina |
| Payload >16MB | Rechazado en `recv_message()` |
