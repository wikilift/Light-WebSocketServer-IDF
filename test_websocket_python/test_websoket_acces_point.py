import asyncio
import websockets
PORT=80
WS_URI = f"ws://192.168.4.1:{PORT}/ws"
RECONNECT_DELAY = 0.1

async def handle_messages():
    print(f"[INFO] Connecting to {WS_URI}...")
    async with websockets.connect(WS_URI, max_size=None) as ws:
        print("[INFO] Connected to WebSocket server.")
        while True:
            try:
                message = await ws.recv()
                if isinstance(message, str):
                    print("[TEXT]", message)
                elif isinstance(message, bytes):
                    print("[BINARY]", message.hex())
                else:
                    print("[UNKNOWN TYPE]", message)
            except websockets.ConnectionClosed:
                print("[WARN] Connection closed by server.")
                raise
            except Exception as e:
                print("[ERROR] Unexpected error:", e)
                raise

async def run_forever():
    while True:
        try:
            await handle_messages()
        except (websockets.ConnectionClosed, ConnectionRefusedError):
            print(f"[INFO] Reconnecting in {RECONNECT_DELAY} seconds...")
            await asyncio.sleep(RECONNECT_DELAY)
        except Exception as e:
            print("[FATAL] Unhandled exception:", e)
            await asyncio.sleep(RECONNECT_DELAY)

if __name__ == "__main__":
    try:
        asyncio.run(run_forever())
    except KeyboardInterrupt:
        print("\n[INFO] Client stopped manually.")
