# Chat Protocol (ADT)

## Data Structure:
- proto-buffer (read the docs if u need them)

## Quick Overview:
- Kinda of the point for using proto-buffering is to have pretty simple data structures, that's why for each different 'method', we'll use a different 'protos' for each one of them.

- Will send the ips and the usernames, the servers will get in charge of managing them


## Unique types
  StatusEnum:
    Active = 0
    DoNotDisturb = 1
    Invisble = 2


## Simple desc


- Client sided (from client to server) [DONE]
  - Registration: [register.proto]
    - Username: string
  - Message general: [message_general.proto]
    - Message: string
    - Status: StatusEnum
    - Username-origin: string
  - Message dm: [message_dm.proto]
    - Message: string
    - Status: StatusEnum
    - Username-des: string
  - Change status: [change_status.proto]
    - Status: StatusEnum
    - Username: string
  - List users: [list_users.proto]
    - Username: string
  - Get user info: [get_user_info.proto]
    - Username-des: string
    - Username: string
  - Quit: [quit.proto]
    - Quit: 0-1

![NOTE]: Protos from the client side will always carry there IP in them, for having notion of which are they

- Server sided (from server to client)
  - All-Users: [all_users.proto]
    - Usernames: strings []
    - Status: StatusEnum []
  - For-dm: [for_dm.proto]
    - Username-des: string
    - Message: string
  - Broadcast delivery: [broadcast_messages.proto]
    - Message: string
    - Username-origin: string
  - Get user info response: [get_user_info_response.proto]
    - Ip-address: string
    - Username: string
    - Status: StatusEnum
  - Server response: [server_response.proto]
    - Status-code: int32
    - Message: string
    - Is-successful: bool
    
    > [NOTE]: Each implementation of this protocol can design their own codes to handle the status of the server response.
