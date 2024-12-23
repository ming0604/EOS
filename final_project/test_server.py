import socket
import sys
import time
import json
import random

def main():
    if len(sys.argv) != 3:
        print(f"Usage: python3 {sys.argv[0]} <server_ip> <server_port>")
        sys.exit(1)

    server_ip = sys.argv[1]
    server_port = int(sys.argv[2])

    # 建立 Socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind((server_ip, server_port))
    server_socket.listen(1)

    print(f"Test server listening on {server_ip}:{server_port}")

    # 等待客戶端連線
    conn, addr = server_socket.accept()
    print(f"Client connected from {addr}")

    screen_width = 800
    screen_height = 600
    player_speed = 2
    bullet_speed = 20
    frame_delay = 0.05  # 每幀延遲（秒）

    # 玩家初始位置和方向
    players = [
        {"id": 1, "x": 100, "y": screen_height - 50, "isAlive": True, "score": 0, "direction": 1, "width": 50},
        {"id": 2, "x": 600, "y": screen_height - 50, "isAlive": True, "score": 0, "direction": -1, "width": 50}
    ]
    
    # 子彈初始狀態
    bullets = []

    # 隨機生成障礙物
    def generate_obstacles(max_obstacles, width, height):
        obstacles = []
        for _ in range(random.randint(1, max_obstacles)):
            obstacles.append({
                "x": random.randint(0, screen_width - width),
                "y": random.randint(50, screen_height // 2),  # 限制障礙物生成在畫面上半部分
                "width": width,
                "height": height,
                "direction": random.choice([-1, 1]),  # 左右移動方向
                "speed": random.randint(1, 3)  # 障礙物速度
            })
        return obstacles

    obstacles = generate_obstacles(max_obstacles=5, width=50, height=50)

    # === 發送 Ready 狀態 ===
    ready_state = {
        "game_state": "Ready",
        "players": [{"id": p["id"]} for p in players]
    }
    conn.sendall(json.dumps(ready_state).encode())
    print("[Server] Sent Ready state")
    time.sleep(3)  # 等待客戶端顯示「Game Ready」畫面

    # === 進入遊戲主迴圈 ===
    for frame in range(1, 500):  # 模擬 500 幀
        # 更新玩家位置
        for player in players:
            player["x"] += player["direction"] * player_speed
            if player["x"] < 0 or player["x"] > screen_width - player["width"]:  # 碰到邊界改變方向
                player["direction"] *= -1

        # 更新子彈位置，移出畫面的子彈將被移除
        bullets = [{"owner": b["owner"], "x": b["x"], "y": b["y"] - bullet_speed} for b in bullets if b["y"] > 0]

        # 隨機讓玩家發射子彈
        for player in players:
            if frame % 30 == 0:  # 每 30 幀發射一次子彈
                bullets.append({
                    "owner": player["id"], 
                    "x": player["x"] ,  # 確保子彈從中心發射
                    "y": player["y"] - 20
                })

        # 更新障礙物位置
        for obstacle in obstacles:
            obstacle["x"] += obstacle["direction"] * obstacle["speed"]
            # 如果障礙物碰到邊界，改變方向
            if obstacle["x"] < 0 or obstacle["x"] > screen_width - obstacle["width"]:
                obstacle["direction"] *= -1
        '''
        # 每 100 幀隨機生成新障礙物
        if frame % 100 == 0:
            obstacles = generate_obstacles(max_obstacles=5, width=50, height=50)
        '''
        # 更新遊戲狀態
        game_state = {
            "game_state": "Game_start",
            "frameCount": frame,
            "gameRemainingTime": max(60 - frame // 10, 0),  # 假設遊戲時間遞減
            "players": players,
            "obstacles": obstacles,
            "bullets": bullets
        }

        # 發送遊戲狀態
        conn.sendall(json.dumps(game_state).encode())
        time.sleep(frame_delay)

    # === 遊戲結束 ===
    game_over_state = {
        "game_state": "Game_over",
        "players": [{"id": p["id"], "score": p["score"]} for p in players],
        "winner": max(players, key=lambda p: p["score"])["id"]
    }
    conn.sendall(json.dumps(game_over_state).encode())
    print("[Server] Sent Game_over state")

    # 關閉連線
    print("[Server] Closing connection")
    conn.close()
    server_socket.close()

if __name__ == "__main__":
    main()
