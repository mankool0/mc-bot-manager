from bot import ScreenState

class ChatMessage:
    sender: str
    content: str
    type: str
    timestamp: int
    is_signed: bool
    sender_uuid: str
    minecraft_chat_type: str

class PlayerState:
    x: float
    y: float
    z: float
    health: float
    hunger: int
    dimension: str

class BaritoneActiveProcess:
    process_name: str
    display_name: str
    priority: int
    is_active: bool
    is_temporary: bool

class BaritoneStatus:
    is_pathing: bool
    event_type: int
    goal_description: str
    active_process: BaritoneActiveProcess
    estimated_ticks_to_goal: int
    ticks_remaining_in_segment: int

class BaritoneLog:
    content: str
    kind: str
    is_error: bool
    title: str
    timestamp: int

class ContainerUpdate:
    id: int
    type: int
    items: list
    x: int
    y: int
    z: int
    properties: dict

class ScriptMessage:
    topic: str
    data: object
    sender_scope: str
    sender_script: str
    timestamp: float

EVENT_HANDLER_PARAMS = {
    'chat_message': 'msg: ChatMessage',
    'player_state': 'state: PlayerState',
    'health_change': 'state: PlayerState',
    'hunger_change': 'state: PlayerState',
    'inventory_update': 'selected_slot: int, items: list',
    'baritone_status_update': 'status: BaritoneStatus',
    'baritone_log': 'log: BaritoneLog',
    'chunk_loaded': 'chunk_x: int, chunk_z: int, dimension: str',
    'chunk_unloaded': 'chunk_x: int, chunk_z: int',
    'block_update': 'x: int, y: int, z: int, block_id: str',
    'multi_block_update': 'count: int',
    'container_update': 'container: ContainerUpdate',
    'screen_updated': 'screen: ScreenState',
    'script_message': 'msg: ScriptMessage',
    'bot_connected': 'bot_name: str',
    'bot_disconnected': 'bot_name: str, uptime_seconds: float',
}
