#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <atomic>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <semaphore.h>
#include <pthread.h>   // for threads
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

// JSON 庫
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// ------------------[ 定義 ]------------------
static const int SERVER_PORT = 8888;
static const int BACKLOG = 5;
static const int BUFFER_SIZE = 1024;
static const int MAX_PLAYERS = 2; // 兩位玩家

// Shared Memory key, 大小
static const key_t SHM_KEY = 0x1234;
static const size_t SHM_SIZE = 4096; // 4KB，示範用

// === 用 pthread_barrier_t 同步 2 位玩家就緒 + 1 個遊戲 ===
static pthread_barrier_t g_barrier;

// ------------------[ 資料結構 ]------------------
struct PlayerInfo {
    float x;
    float y;
    bool  isAlive;
    bool  isShootingDisabled; // NEW: 禁用射擊
    int   score;
};

struct GameState {
    std::atomic<bool>   gameOver;
    int                 gameRemainingTime;  
    int                 bulletTimerCount;   
    int                 obstacleTimerCount; 
    PlayerInfo          player[2];
    int meteorTarget;
    int meteorCounter;

    char bulletsJSON[1024];
    char obstaclesJSON[1024];
};

// ------------------[ 全域變數 ]------------------
int  g_playerFd[2] = {-1, -1};
int  g_serverFd    = -1;


sem_t*     g_semGame   = nullptr;
int        g_shmid     = -1;
GameState* g_gamePtr   = nullptr; 

bool g_playerReady[2]  = {false, false};

pthread_t g_gameThread;
pthread_t g_playerThread[MAX_PLAYERS];

// ------------------[ 遊戲常數 ]------------------
constexpr int tick_rate = 5;
constexpr int tick_period = 1000000000 / tick_rate;

constexpr int game_length = 30;
constexpr int bullet_period = 1;
constexpr int bullet_tick_period = tick_rate * bullet_period;
constexpr int obstacle_spawn_period = 2;
constexpr int obstacle_spawn_tick_period = tick_rate * obstacle_spawn_period;
constexpr int initial_obstacle_quantity = 5;
constexpr int periodic_obstacle_quanity = 1;
constexpr int meteor_period = 23;
constexpr int meteor_duration = 8;
constexpr int meteor_tick_duration = tick_rate * meteor_duration;

constexpr float player_speed = 30.0f;
constexpr float bullet_speed = 120.0f / tick_rate;
constexpr float obstacle_maximum_speed = 135.0f / tick_rate;

// ------------------[ 工具函式 ]------------------
std::string recvFromClient(int fd)
{
    char buf[BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));
    ssize_t ret = recv(fd, buf, sizeof(buf) - 1, 0);
    if (ret <= 0) {
        return "";
    }
    return std::string(buf);
}

bool sendJsonToClient(int fd, const json& jdata)
{
    std::string jsonStr = jdata.dump() + "\n";
    // std::cout << "[Debug] Sending to client: " << jsonStr << std::endl;
    ssize_t ret = send(fd, jsonStr.c_str(), jsonStr.size(), 0);
    return (ret > 0);
}

std::vector<json> parseJsonArray(const char* arrStr)
{
    std::vector<json> result;
    try {
        auto j = json::parse(arrStr);
        if (j.is_array()) {
            for (auto& elem : j) {
                result.push_back(elem);
            }
        }
    } catch (...) {
        // parse 失敗就回空
    }
    return result;
}

void writeJsonArray(char* dest, const std::vector<json>& arr)
{
    json jarr = json::array();
    for (auto& e : arr) {
        jarr.push_back(e);
    }
    std::string s = jarr.dump();
    strncpy(dest, s.c_str(), 1023);
    dest[1023] = '\0';
}
/*
// ------------------[ AABB 碰撞檢測函式 ]------------------
// 注意：假設 x, y 為「左上角」座標，w, h 為物體的寬高
bool checkAABBCollision(float x1, float y1, float w1, float h1, 
                        float x2, float y2, float w2, float h2)
{
    return !(x1 > (x2 + w2)   ||  // 物件1在物件2右側
             (x1 + w1) < x2  ||  // 物件1在物件2左側
             y1 > (y2 + h2)  ||  // 物件1在物件2下方
             (y1 + h1) < y2);    // 物件1在物件2上方
}
*/
bool checkAABBCollisionCenter(float cx1, float cy1, float w1, float h1,
                              float cx2, float cy2, float w2, float h2)
{
    // 物體1的四個邊
    float left1   = cx1 - w1 * 0.5f;
    float right1  = cx1 + w1 * 0.5f;
    float top1    = cy1 - h1 * 0.5f;
    float bottom1 = cy1 + h1 * 0.5f;

    // 物體2的四個邊
    float left2   = cx2 - w2 * 0.5f;
    float right2  = cx2 + w2 * 0.5f;
    float top2    = cy2 - h2 * 0.5f;
    float bottom2 = cy2 + h2 * 0.5f;

    // 如果「一方在另一方的完全左邊」或「一方在另一方的完全右邊」或
    // 「一方在另一方的完全上邊」或「一方在另一方的完全下邊」 ==> 沒有碰撞
    if (right1 < left2   ||  // 物體1 在 物體2 的左邊
        left1 > right2   ||  // 物體1 在 物體2 的右邊
        bottom1 < top2   ||  // 物體1 在 物體2 的上邊
        top1 > bottom2)      // 物體1 在 物體2 的下邊
    {
        return false;
    }
    // 否則重疊
    return true;
}

// ------------------[ Signal Handler ]------------------
void gameUpdateHandler(int signum)
{
    sem_wait(g_semGame);
    auto& st = *g_gamePtr;

    if (st.gameOver) {
        sem_post(g_semGame);
        return;
    }

    // 如果遊戲時間進行到一半，兩名玩家存活，隨機選擇一名玩家禁用射擊
    if (st.gameRemainingTime == meteor_period && st.player[0].isAlive && st.player[1].isAlive && st.meteorTarget == -1) {
        int targetPlayer = std::rand() % 2; // 隨機選擇玩家 0 或 1
        st.player[targetPlayer].isShootingDisabled = true;
        st.meteorTarget = targetPlayer;
        st.meteorCounter = 0; // 開始計數

        std::cout << "[GameUpdate] Player " << (targetPlayer + 1) << " is hit by meteor!\n";
    }

    // 如果隕石禁用計時器正在運行，則更新計數器
    if (st.meteorTarget != -1) {
        st.meteorCounter++;
        if (st.meteorCounter >= meteor_tick_duration) { 
            st.player[st.meteorTarget].isShootingDisabled = false;
            std::cout << "[GameUpdate] Player " << (st.meteorTarget + 1) << "'s shooting restored!\n";

            st.meteorTarget = -1;   // 重置隕石目標
            st.meteorCounter = 0;  // 重置計數器
        }
    }

    // -------------------------
    // 1) 子彈更新(移動) 和 篩選
    // -------------------------
    std::vector<json> bulletVec = parseJsonArray(st.bulletsJSON);
    for (auto& bullet : bulletVec) {
        float bx = bullet["x"].get<float>();
        float by = bullet["y"].get<float>();
        // 子彈向上移動
        by -= bullet_speed;
        bullet["x"] = bx;
        bullet["y"] = by;
    }
    // 範圍檢查 (保留在螢幕中的子彈)
    std::vector<json> newBullets;
    newBullets.reserve(bulletVec.size());
    for (auto& b : bulletVec) {
        float bx = b["x"].get<float>();
        float by = b["y"].get<float>();
        if (bx >= 0 && bx <= 800 && by >= 0 && by <= 600) {
            newBullets.push_back(b);
        }
    }
    // 每秒新增子彈(示範)
    st.bulletTimerCount++;
    if (st.bulletTimerCount >= bullet_tick_period) {
        st.bulletTimerCount = 0;

        if (st.player[0].isAlive && !st.player[0].isShootingDisabled) {
            json b1;
            b1["owner"] = 1;
            b1["x"]     = st.player[0].x;
            b1["y"]     = st.player[0].y - 10;
            newBullets.push_back(b1);
        }
        if (st.player[1].isAlive && !st.player[1].isShootingDisabled) {
            json b2;
            b2["owner"] = 2;
            b2["x"]     = st.player[1].x;
            b2["y"]     = st.player[1].y - 10;
            newBullets.push_back(b2);
        }
    }

    // 2) 更新 bulletsJSON
    writeJsonArray(st.bulletsJSON, newBullets);

    // 3) 定時新增障礙物 (每 10 秒)
    st.obstacleTimerCount++;
    if (st.obstacleTimerCount >= obstacle_spawn_tick_period) {
        st.obstacleTimerCount = 0;
        std::vector<json> obsVec = parseJsonArray(st.obstaclesJSON);
        for (int i_obs=0; i_obs<periodic_obstacle_quanity; i_obs++) {
            json obj;
            obj["x"] = (std::rand() % 600) + 100;
            obj["y"] = (std::rand() % 300) + 100;
            obsVec.push_back(obj);
        }
        writeJsonArray(st.obstaclesJSON, obsVec);
        std::cout << "[GameThread] 10s -> Add obstacle.\n";
    }

    // 4) 讓障礙物移動 & 牆壁反彈
    std::vector<json> obstacles = parseJsonArray(st.obstaclesJSON);
    for (auto& obs : obstacles) 
    {
        if (!obs.contains("vx") || !obs.contains("vy")) {
            float vx = static_cast<float>((std::rand() % (int)obstacle_maximum_speed) - (int)obstacle_maximum_speed / 2);
            float vy = static_cast<float>((std::rand() % (int)obstacle_maximum_speed) - (int)obstacle_maximum_speed / 2);
            vx *= 2.0f;
            vy *= 2.0f;
            obs["vx"] = vx;
            obs["vy"] = vy;
        }
        float ox = obs["x"].get<float>();
        float oy = obs["y"].get<float>();
        float vx = obs["vx"].get<float>();
        float vy = obs["vy"].get<float>();

        // 移動
        ox += vx;
        oy += vy;

        // 與邊界碰撞反彈 (x: 0~800, y: 0~600)
        if (ox < 0) {
            ox = 0;
            vx = -vx;
        } else if (ox > 800) {
            ox = 800;
            vx = -vx;
        }
        if (oy < 0) {
            oy = 0;
            vy = -vy;
        } else if (oy > 600) {
            oy = 600;
            vy = -vy;
        }
        obs["x"]  = ox;
        obs["y"]  = oy;
        obs["vx"] = vx;
        obs["vy"] = vy;
    }
    writeJsonArray(st.obstaclesJSON, obstacles);

    // -----------------------------------------------------------------
    // 5) 判斷 子彈 與 障礙物 碰撞 (AABB 取代距離判斷)
    //    (擊中就移除子彈+障礙物)
    // -----------------------------------------------------------------
    // 定義子彈、障礙物在 AABB 檢測時的寬高 (可自行調整)
    const float bulletW = 5.0f;
    const float bulletH = 5.0f;
    const float obstacleW = 90.0f;
    const float obstacleH = 60.0f;

    std::vector<json> updatedBullets; 
    updatedBullets.reserve(newBullets.size());
    std::vector<json> updatedObstacles;
    updatedObstacles.reserve(obstacles.size());

    for (auto& obs : obstacles) {
        bool obstacleDestroyed = false;
        float ox = obs["x"].get<float>();
        float oy = obs["y"].get<float>();

        std::vector<json> tmpBullets;
        for (auto& b : newBullets) {
            float bx = b["x"].get<float>();
            float by = b["y"].get<float>();

            // 使用 AABB 判斷取代「距離」判斷
            if (checkAABBCollisionCenter(bx, by, bulletW, bulletH,
                                   ox, oy, obstacleW, obstacleH))
            {
                // 碰撞 -> 障礙物毀、子彈消失
                obstacleDestroyed = true;
                int owner = b["owner"];
                st.player[owner - 1].score += 1; 
            }
            else {
                tmpBullets.push_back(b);
            }
        }

        newBullets = tmpBullets;

        if (!obstacleDestroyed) {
            updatedObstacles.push_back(obs);
        }
    }
    updatedBullets.insert(updatedBullets.end(),
                          newBullets.begin(), newBullets.end());

    // -----------------------------------------------------------------
    // 6) 判斷 障礙物 與 玩家 碰撞 (AABB 取代距離判斷)
    //    (若碰撞，玩家 isAlive = false)
    // -----------------------------------------------------------------
    // 定義玩家、障礙物在 AABB 檢測時的寬高 (可自行調整)
    const float playerW = 30.0f;
    const float playerH = 70.0f;
    for (auto& obs : updatedObstacles) {
        float ox = obs["x"].get<float>();
        float oy = obs["y"].get<float>();

        for (int i = 0; i < 2; i++) {
            if (st.player[i].isAlive) {
                float px = st.player[i].x;
                float py = st.player[i].y;

                // 使用 AABB 判斷
                if (checkAABBCollisionCenter(px, py, playerW, playerH,
                                       ox, oy, obstacleW, obstacleH))
                {
                    st.player[i].isAlive = false;
                }
            }
        }
    }

    // -----------------------------------------------------------------
    // 7) 寫回新的障礙物列表 & 子彈列表
    // -----------------------------------------------------------------
    writeJsonArray(st.obstaclesJSON, updatedObstacles);
    writeJsonArray(st.bulletsJSON,   updatedBullets);

    // -----------------------------------------------------------------
    // 8) 遊戲剩餘時間倒數 (原邏輯)
    // -----------------------------------------------------------------
    static int one_sec = tick_rate;
    one_sec--;
    if (one_sec == 0) {
        one_sec = tick_rate;
        st.gameRemainingTime--;
    }

    // -----------------------------------------------------------------
    // 9) 判斷是否兩個玩家皆死亡，或是遊戲時間到
    // -----------------------------------------------------------------
    st.gameOver = (!st.player[0].isAlive && !st.player[1].isAlive) || st.gameRemainingTime == 0;

    // 其餘原邏輯 (或直接結束)
    sem_post(g_semGame);
}

// ------------------[ Game Thread ]------------------
void* runGameThread(void* arg)
{
    std::cout << "[GameThread] Started.\n";

    // 等待 players ready
    std::cout << "game set for 2 players!" << std::endl;
    pthread_barrier_wait(&g_barrier);
    std::cout << "game ready for 2 players!" << std::endl;

    // 初始化五個障礙物
    sem_wait(g_semGame);
    auto& st = *g_gamePtr;
    std::vector<json> obsVec = parseJsonArray(st.obstaclesJSON);
    for (int i_obs = 0; i_obs < initial_obstacle_quantity; i_obs++) {
        json obj;
        obj["x"] = (std::rand() % 600) + 100;
        obj["y"] = (std::rand() % 300) + 100;
        obsVec.push_back(obj);
        writeJsonArray(st.obstaclesJSON, obsVec);
    }
    sem_post(g_semGame);

    // sigaction
    {
        struct sigaction sb;
        memset(&sb, 0, sizeof(sb));
        sb.sa_handler = gameUpdateHandler;
        sigaction(SIGUSR1, &sb, NULL);
    }

    // 每秒週期 SIGUSR1 (改成 0.05s)
    {
        timer_t timerid;
        struct sigevent sev;
        memset(&sev, 0, sizeof(sev));
        sev.sigev_notify = SIGEV_SIGNAL;
        sev.sigev_signo  = SIGUSR1;
        if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
            std::cerr << "[GameThread] timer_create failed\n";
        }
        struct itimerspec its;
        its.it_value.tv_sec = 1;
        its.it_value.tv_nsec = 0;
        // its.it_interval.tv_sec = 1;
        its.it_interval.tv_sec = 0;
        // its.it_interval.tv_nsec = 0;
        its.it_interval.tv_nsec = tick_period;
        timer_settime(timerid, 0, &its, nullptr);
    }

    // 遊戲主迴圈
    while (true) {
        sem_wait(g_semGame);
        bool over = g_gamePtr->gameOver;
        sem_post(g_semGame);
        if (over) break;
        usleep(100000); // 0.1s
    }

    std::cout << "[GameThread] End.\n";
    pthread_exit(nullptr);
    return nullptr;
}

// ----------------------------------------------------------------
// 在 Player “Thread” 中，我們使用「雙線程」
//
//  Thread1: 讀取玩家指令 -> 更新 shared memory 
//  Thread2: 傳送「遊戲狀態」(JSON) 給 client
// ----------------------------------------------------------------
struct PlayerThreadArgs {
    int playerIdx; 
    int sockFd;
};

// Thread1 (讀玩家輸入):
void* playerInputThread(void* arg)
{
    auto* info = (PlayerThreadArgs*)arg;
    int idx = info->playerIdx;
    int fd  = info->sockFd;

    std::cout << "[Player" << (idx+1) 
              << "] Thread1 (Input) start.\n";

    g_playerReady[idx] = true; // 標記自己就緒


    while (true) {
        sem_wait(g_semGame);
        bool over = g_gamePtr->gameOver;
        sem_post(g_semGame);
        if (over) break;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;  
        tv.tv_usec = 200000; // 0.2s
        int ret = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break; 
        }
        if (ret > 0 && FD_ISSET(fd, &readfds)) {
            std::string input = recvFromClient(fd);
            if (input.empty()) {
                // client 斷線
                break;
            }
            // [修改] 直接解析字串: "Left button state: X, Right button state: Y"
            // std::cout << "[Debug] Received from client: " << input << std::endl;
            int left = 0;
            int right = 0;
            if (sscanf(input.c_str(), "Left button state: %d, Right button state: %d", &left, &right) == 2)
            {
                sem_wait(g_semGame);
                if (left == 1) {
                    g_gamePtr->player[idx].x -= player_speed;
                }
                if (right == 1) {
                    g_gamePtr->player[idx].x += player_speed;
                }
                // 確保玩家位置在範圍內
                if (g_gamePtr->player[idx].x < 30) {
                    g_gamePtr->player[idx].x = 30;
                }
                if (g_gamePtr->player[idx].x > 770) {
                    g_gamePtr->player[idx].x = 770;
                }
                sem_post(g_semGame);
            }
        }
    }

    std::cout << "[Player" << (idx+1) 
              << "] Thread1 (Input) end.\n";
    return nullptr;
}

// Thread2 (傳送遊戲狀態 JSON):
void* playerSendThread(void* arg)
{
    auto* info = (PlayerThreadArgs*)arg;
    int idx = info->playerIdx;
    int fd  = info->sockFd;

    std::cout << "[Player" << (idx+1) 
              << "] Thread2 (Send) start.\n";

    while (true) {
        sem_wait(g_semGame);
        bool over    = g_gamePtr->gameOver;
        bool isAlive = g_gamePtr->player[idx].isAlive;

        json stateMsg;
        stateMsg["game_state"] = "Game_start";
        stateMsg["gameRemainingTime"] = g_gamePtr->gameRemainingTime;
        
        json arr = json::array();
        for (int i=0; i<2; i++){
            json pp;
            pp["id"]      = i+1;
            pp["x"]       = g_gamePtr->player[i].x;
            pp["y"]       = g_gamePtr->player[i].y;
            pp["isAlive"] = g_gamePtr->player[i].isAlive;
            pp["score"]   = g_gamePtr->player[i].score;
            pp["isShootingDisabled"] = g_gamePtr->player[i].isShootingDisabled; // NEW
            arr.push_back(pp);
        }
        stateMsg["players"]   = arr;
        stateMsg["bullets"]   = json::parse(g_gamePtr->bulletsJSON);
        stateMsg["obstacles"] = json::parse(g_gamePtr->obstaclesJSON);
        sem_post(g_semGame);

        if (!sendJsonToClient(fd, stateMsg)) {
            break;
        }
        // if (!isAlive) break; // 若玩家掛了就結束傳送
        if (over) break;       // 或遊戲結束就停止傳送

        usleep(200000); // 0.2s 間隔
    }

    std::cout << "[Player" << (idx+1) 
              << "] Thread2 (Send) end.\n";
    return nullptr;
}

void* runPlayerThread(void* threadArg)
{
    int playerIdx = *(int*)threadArg; 
    int fd        = g_playerFd[playerIdx];

    std::cout << "[PlayerThread] Player" << (playerIdx+1) 
              << " started.\n";

    // [ADD] Ready 狀態：傳送 JSON 給該玩家
    {
        sem_wait(g_semGame);
        json readyMsg;
        readyMsg["game_state"] = "Ready";
        json arr = json::array();

        json p;
        p["id"]      = playerIdx + 1;
        p["x"]       = g_gamePtr->player[playerIdx].x;
        p["y"]       = g_gamePtr->player[playerIdx].y;
        p["isAlive"] = g_gamePtr->player[playerIdx].isAlive;
        p["score"]   = g_gamePtr->player[playerIdx].score;
        arr.push_back(p);

        readyMsg["players"] = arr;
        sem_post(g_semGame);

        sendJsonToClient(fd, readyMsg);
    }
    
    // 等待大家就緒
    std::cout << "game set for player " << playerIdx << std::endl;
    sleep(8); // 保留與原程式相同的 sleep
    pthread_barrier_wait(&g_barrier);
    std::cout << "game started for player " << playerIdx << std::endl;

    // 建立子線程
    pthread_t tInput, tSend;
    PlayerThreadArgs args;
    args.playerIdx = playerIdx;
    args.sockFd    = fd;

    pthread_create(&tInput, nullptr, playerInputThread, &args);
    pthread_create(&tSend,  nullptr, playerSendThread,  &args);

    // 等待子線程結束
    pthread_join(tInput, nullptr);
    pthread_join(tSend,  nullptr);

    sem_wait(g_semGame);
    json endMsg;
    endMsg["game_state"] = "Game_over";
    json arr = json::array();
    for (int i=0; i<2; i++){
        json pp;
        pp["id"]      = i+1;
        pp["isAlive"] = g_gamePtr->player[i].isAlive;
        pp["score"]   = g_gamePtr->player[i].score;
        arr.push_back(pp);
    }
    endMsg["players"] = arr;
    if (g_gamePtr->player[0].score > g_gamePtr->player[1].score) {
        endMsg["winner"] = 1;
    } else if (g_gamePtr->player[0].score < g_gamePtr->player[1].score) {
        endMsg["winner"] = 2;
    } else {
        endMsg["winner"] = 0;
    }
    sem_post(g_semGame);

    sendJsonToClient(fd, endMsg);

    sleep(10);
    close(fd);

    std::cout << "[PlayerThread] Player" << (playerIdx+1) 
              << " end.\n";
    pthread_exit(nullptr);
    return nullptr;
}

// ------------------[ Main Thread (程序入口) ]------------------
int main()
{
    srand(time(NULL));

    pthread_barrier_init(&g_barrier, nullptr, MAX_PLAYERS + 1);

    g_shmid = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT | 0666);
    if (g_shmid < 0) {
        std::cerr << "[Main] shmget failed\n";
        return 1;
    }
    g_gamePtr = (GameState*)shmat(g_shmid, NULL, 0);
    if ((void*)g_gamePtr == (void*)-1) {
        std::cerr << "[Main] shmat failed\n";
        return 1;
    }
    memset(g_gamePtr, 0, sizeof(GameState));
    g_gamePtr->gameOver           = false;
    g_gamePtr->gameRemainingTime  = game_length;
    g_gamePtr->meteorTarget = -1;
    g_gamePtr->meteorCounter = 0;
    g_gamePtr->bulletTimerCount   = 0;
    g_gamePtr->obstacleTimerCount = 0;

    g_gamePtr->player[0] = {250.0f, 550.0f, true, 0};
    g_gamePtr->player[1] = {500.0f, 550.0f, true, 0};

    strcpy(g_gamePtr->bulletsJSON, "[]");
    strcpy(g_gamePtr->obstaclesJSON, "[]");

    sem_unlink("/gameSem");
    g_semGame = sem_open("/gameSem", O_CREAT, 0666, 1);
    if (g_semGame == SEM_FAILED) {
        std::cerr << "[Main] sem_open failed\n";
        return 1;
    }

    if (pthread_create(&g_gameThread, nullptr, runGameThread, nullptr) != 0) {
        std::cerr << "[Main] pthread_create for GameThread failed\n";
        return 1;
    }

    g_serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_serverFd < 0) {
        std::cerr << "[Main] cannot create socket\n";
        return 1;
    }
    int opt = 1;
    setsockopt(g_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(SERVER_PORT);

    if (bind(g_serverFd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[Main] bind failed\n";
        close(g_serverFd);
        return 1;
    }
    if (listen(g_serverFd, BACKLOG) < 0) {
        std::cerr << "[Main] listen failed\n";
        close(g_serverFd);
        return 1;
    }
    std::cout << "[Main] Listening on port " << SERVER_PORT << "...\n";

    for (int i = 0; i < MAX_PLAYERS; i++) {
        sockaddr_in caddr;
        socklen_t caddrLen = sizeof(caddr);
        int cfd = accept(g_serverFd, (struct sockaddr*)&caddr, &caddrLen);
        if (cfd < 0) {
            std::cerr << "[Main] accept for player " << i << " failed.\n";
            return 1;
        }
        g_playerFd[i] = cfd;
        std::cout << "[Main] Player" << (i+1) << " connected.\n";

        static int playerIndices[MAX_PLAYERS] = {0,1};
        if (pthread_create(&g_playerThread[i], nullptr,
                           runPlayerThread,
                           &playerIndices[i]) != 0) 
        {
            std::cerr << "[Main] pthread_create for PlayerThread failed\n";
            return 1;
        }
    }

    while (true) {
        sem_wait(g_semGame);
        bool over = g_gamePtr->gameOver;
        sem_post(g_semGame);
        if (over) break;
        sleep(1);
    }

    std::cout << "[Main] GameOver, Cleaning up...\n";

    pthread_join(g_gameThread, nullptr);
    for (int i = 0; i < MAX_PLAYERS; i++){
        pthread_join(g_playerThread[i], nullptr);
    }

    close(g_serverFd);
    shmdt(g_gamePtr);
    shmctl(g_shmid, IPC_RMID, NULL);
    sem_close(g_semGame);
    sem_unlink("/gameSem");

    pthread_barrier_destroy(&g_barrier);
    std::cout << "[Main] End.\n";
    return 0;
}