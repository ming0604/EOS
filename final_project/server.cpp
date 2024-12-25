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
    int   score;
};

struct GameState {
    // bool  gameOver;
    std::atomic<bool>   gameOver;
    int                 gameRemainingTime;  
    int                 bulletTimerCount;   
    int                 obstacleTimerCount; 
    PlayerInfo          player[2];

    char bulletsJSON[1024];
    char obstaclesJSON[1024];
};

// ------------------[ 全域變數 ]------------------
int  g_playerFd[2] = {-1, -1};
int  g_serverFd    = -1;

sem_t*     g_semGame   = nullptr;
int        g_shmid     = -1;
GameState* g_gamePtr   = nullptr; 

// 用來標示「某位玩家是否已經進入 Barrier 等待」
bool g_playerReady[2] = {false, false};

// Threads
pthread_t g_gameThread;
pthread_t g_playerThread[MAX_PLAYERS];

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
    std::string jsonStr = jdata.dump();

    // [新增] 為了觀察傳出去的JSON資料
    std::cout << "[Debug] Sending to client: " << jsonStr << std::endl;

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

// ------------------[ Signal Handler ]------------------
void gameEndHandler(int signum)
{
    // std::cout << "[GameThread] GameEnd Timer triggered.\n" << std::flush;
    // if (sem_trywait(g_semGame) != 0) std::cout << "g_semGame is locked!" << std::flush;
    // sem_wait(g_semGame);
    g_gamePtr->gameOver = true;
    // g_gamePtr->gameOver.store(true, std::memory_order_relaxed);
    // sem_post(g_semGame);
}

// void gameUpdateHandler(int signum)
// {
//     sem_wait(g_semGame);
//     auto& st = *g_gamePtr;

//     // -- 子彈更新(移動) --
//     std::vector<json> bulletVec = parseJsonArray(st.bulletsJSON);
//     for (auto& bullet : bulletVec) {
//         float bx = bullet["x"].get<float>();
//         float by = bullet["y"].get<float>();
//         bx += 5.0f; 
//         bullet["x"] = bx;
//         bullet["y"] = by;
//     }
//     // 範圍檢查
//     std::vector<json> newBullets;
//     for (auto& b : bulletVec) {
//         float bx = b["x"].get<float>();
//         float by = b["y"].get<float>();
//         if (bx >= 0 && bx <= 800 && by >= 0 && by <= 600) {
//             newBullets.push_back(b);
//         }
//     }
//     // 每秒新增子彈(示範)
//     st.bulletTimerCount++;
//     {
//         json b1;
//         b1["owner"] = 1;
//         b1["x"]     = st.player[0].x;
//         b1["y"]     = st.player[0].y;
//         newBullets.push_back(b1);

//         json b2;
//         b2["owner"] = 2;
//         b2["x"]     = st.player[1].x;
//         b2["y"]     = st.player[1].y;
//         newBullets.push_back(b2);
//     }
//     writeJsonArray(st.bulletsJSON, newBullets);

//     // 每10秒加障礙物
//     st.obstacleTimerCount++;
//     if (st.obstacleTimerCount >= 10) {
//         st.obstacleTimerCount = 0;
//         std::vector<json> obsVec = parseJsonArray(st.obstaclesJSON);
//         json obj;
//         obj["x"] = (int)st.player[0].x + (int)st.player[1].x;
//         obj["y"] = 50;
//         obsVec.push_back(obj);
//         writeJsonArray(st.obstaclesJSON, obsVec);
//         std::cout << "[GameThread] 10s -> Add obstacle.\n";
//     }
//     sem_post(g_semGame);
// }

void gameUpdateHandler(int signum)
{
    sem_wait(g_semGame);
    auto& st = *g_gamePtr;

    if(st.gameOver){
        sem_post(g_semGame);
        return;
    }

    // -------------------------
    // 1) 子彈更新(移動) 和 篩選
    // -------------------------
    std::vector<json> bulletVec = parseJsonArray(st.bulletsJSON);
    for (auto& bullet : bulletVec) {
        float bx = bullet["x"].get<float>();
        float by = bullet["y"].get<float>();
        // 子彈向上移動
        by -= 5.0f;
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
    // 每秒新增子彈(示範) - 不改動
    st.bulletTimerCount++;
    if (st.bulletTimerCount >= 20) {
        st.bulletTimerCount = 0;

        if (st.player[0].isAlive) {
            json b1;
            b1["owner"] = 1;
            b1["x"]     = st.player[0].x;
            b1["y"]     = st.player[0].y - 10;
            newBullets.push_back(b1);
        }

        if (st.player[1].isAlive) {
            json b2;
            b2["owner"] = 2;
            b2["x"]     = st.player[1].x;
            b2["y"]     = st.player[1].y - 10;
            newBullets.push_back(b2);
        }
    }

    // -------------------------
    // 2) 更新 bulletsJSON
    // -------------------------
    writeJsonArray(st.bulletsJSON, newBullets);

    // -------------------------
    // 3) 定時新增障礙物 (每 10 秒)
    // -------------------------
    st.obstacleTimerCount++;
    if (st.obstacleTimerCount >= 200) {
        st.obstacleTimerCount = 0;
        std::vector<json> obsVec = parseJsonArray(st.obstaclesJSON);
        for (int i_obs=0; i_obs<5; i_obs++) {
            json obj;
            obj["x"] = (std::rand() % 600) + 100;
            obj["y"] = (std::rand() % 300) + 100;
            // 下面兩行可先不給速度，讓後續程式隨機產生
            // obj["vx"] = 0;
            // obj["vy"] = 0;
            obsVec.push_back(obj);
        }
        writeJsonArray(st.obstaclesJSON, obsVec);
        std::cout << "[GameThread] 10s -> Add obstacle.\n";
    }

    // -----------------------------------------------------------------
    // 4) 讓障礙物移動 & 牆壁反彈
    // -----------------------------------------------------------------
    std::vector<json> obstacles = parseJsonArray(st.obstaclesJSON);
    for (auto& obs : obstacles) 
    {
        // 如果該障礙物還沒有速度(vx, vy)，就隨機給一個
        if (!obs.contains("vx") || !obs.contains("vy")) {
            float vx = static_cast<float>((std::rand() % 10) - 5); // -3 ~ 3
            float vy = static_cast<float>((std::rand() % 10) - 5);
            // if (vx == 0) vx = 1.0f;  // 避免 0
            // if (vy == 0) vy = 1.0f;
            vx *= 1.5f;
            vy *= 1.5f;
            obs["vx"] = vx;
            obs["vy"] = vy;
        }
        // 讀取資訊
        float ox = obs["x"].get<float>();
        float oy = obs["y"].get<float>();
        float vx = obs["vx"].get<float>();
        float vy = obs["vy"].get<float>();

        // 移動
        ox += vx;
        oy += vy;

        // 與邊界碰撞反彈 (x: 0~800, y: 0~600)
        if (ox < 0) {
            ox = 0;   // 校正
            vx = -vx; // 反彈
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

        // 寫回
        obs["x"]  = ox;
        obs["y"]  = oy;
        obs["vx"] = vx;
        obs["vy"] = vy;
    }
    writeJsonArray(st.obstaclesJSON, obstacles);

    // -----------------------------------------------------------------
    // 5) 判斷 子彈 與 障礙物 碰撞
    //    (擊中就移除子彈+障礙物)
    // -----------------------------------------------------------------
    std::vector<json> updatedBullets; 
    updatedBullets.reserve(newBullets.size());

    // 我們將先保留全部 obstacle，待會再剔除被擊中的
    std::vector<json> updatedObstacles;
    updatedObstacles.reserve(obstacles.size());

    // 設定一個碰撞半徑
    const float bulletHitRadius = 25.0f;

    // 逐一檢查障礙物有沒有被子彈擊中
    for (auto& obs : obstacles) {
        bool obstacleDestroyed = false;
        float ox = obs["x"].get<float>();
        float oy = obs["y"].get<float>();

        // 一個障礙物，可能被多顆子彈撞到，但只要撞一次就毀
        // 所以先把 bulletVec 全部檢查完，再加到 updatedBullets
        std::vector<json> tmpBullets;

        for (auto& b : newBullets) {
            float bx = b["x"].get<float>();
            float by = b["y"].get<float>();
            float dx = bx - ox;
            float dy = by - oy;
            float dist2 = dx*dx + dy*dy; // 距離平方

            if (dist2 < (bulletHitRadius * bulletHitRadius)) {
                // 子彈與障礙物碰撞 -> 該障礙物被毀，子彈也消失
                obstacleDestroyed = true;
                // 不將這顆子彈推入 tmpBullets (因為它被銷毀)
                int owner = b["owner"];
                st.player[owner - 1].score += 1;
            }
            else {
                // 沒有撞到 -> 保留子彈
                tmpBullets.push_back(b);
            }
        }

        // 這次檢查完後，「沒撞到的子彈」成為下一輪基準
        newBullets = tmpBullets;

        // 如果障礙物沒被銷毀，就保留
        if (!obstacleDestroyed) {
            updatedObstacles.push_back(obs);
        }
    }
    // 更新完後，剩下的新子彈集合才是真正的存活子彈
    updatedBullets.insert(updatedBullets.end(),
                          newBullets.begin(), newBullets.end());

    // -----------------------------------------------------------------
    // 6) 判斷 障礙物 與 玩家 碰撞
    //    (若碰撞，玩家 isAlive = false)
    // -----------------------------------------------------------------
    const float obstacleHitRadius = 50.0f;
    for (auto& obs : updatedObstacles) {
        float ox = obs["x"].get<float>();
        float oy = obs["y"].get<float>();
        for (int i = 0; i < 2; i++) {
            if (st.player[i].isAlive) {
                float px = st.player[i].x;
                float py = st.player[i].y;
                float dx = px - ox;
                float dy = py - oy;
                float dist2 = dx*dx + dy*dy;
                // 碰到就把玩家標記成死亡
                if (dist2 < (obstacleHitRadius * obstacleHitRadius)) {
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
    // 8) 遊戲剩餘時間倒數
    // -----------------------------------------------------------------
    static int one_sec = 20;
    one_sec--;
    if (one_sec == 0){
        one_sec = 20;
        st.gameRemainingTime--;
    }

    // -----------------------------------------------------------------
    // 9) 判斷是否兩個玩家皆死亡
    // -----------------------------------------------------------------
    st.gameOver = !st.player[0].isAlive && !st.player[1].isAlive;

    // -----------------------------------------------------------------
    // 9) 判斷是否遊戲時間到
    // -----------------------------------------------------------------
    // static int game_duration = 1200;
    // game_duration--;
    // if (game_duration == 0) {
    //     st.gameOver = true;
    // }

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
    for (int i_obs=0; i_obs<5; i_obs++) {
        json obj;
        obj["x"] = (std::rand() % 600) + 100;
        obj["y"] = (std::rand() % 300) + 100;
        obsVec.push_back(obj);
        writeJsonArray(st.obstaclesJSON, obsVec);
    }
    sem_post(g_semGame);

    // sigaction
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = gameEndHandler;
        sigaction(SIGALRM, &sa, NULL);

        struct sigaction sb;
        memset(&sb, 0, sizeof(sb));
        sb.sa_handler = gameUpdateHandler;
        sigaction(SIGUSR1, &sb, NULL);
    }

    // 遊戲結束計時器(60秒)
    {
        struct itimerval itv;
        itv.it_value.tv_sec = 30;
        itv.it_value.tv_usec = 0;
        itv.it_interval.tv_sec = 0;
        itv.it_interval.tv_usec = 0;
        setitimer(ITIMER_REAL, &itv, nullptr);
    }
    // 每秒週期 SIGUSR1
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
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 50000000;
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
//  Thread1: 讀「硬體/裝置輸入」(從 socket 收到自訂格式字串)
//           -> 更新 shared memory 
//  Thread2: 傳送「遊戲狀態」(JSON) 給 client
// ----------------------------------------------------------------

// 結構: 用於 pthread_create 傳遞參數
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

    // 標記自己就緒
    g_playerReady[idx] = true;
    // 等 2 位 playerReady = true

    while (true) {
        // 檢查遊戲結束
        sem_wait(g_semGame);
        bool over = g_gamePtr->gameOver;
        sem_post(g_semGame);
        if (over) break;

        // 用 select 等待 client 傳來指令
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;  
        tv.tv_usec = 200000; // 0.2s
        int ret = select(fd+1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break; 
        }
        if (ret > 0 && FD_ISSET(fd, &readfds)) {
            std::string input = recvFromClient(fd);
            if (input.empty()) {
                // client 斷線
                // sem_wait(g_semGame);
                // g_gamePtr->player[idx].isAlive = false;
                // sem_post(g_semGame);
                break;
            }

            // [修改] 直接解析字串: "Left button state: X, Right button state: Y"
            std::cout << "[Debug] Received from client: " << input << std::endl;
            int left = 0;
            int right = 0;
            // 使用 sscanf 簡化
            if (sscanf(input.c_str(),
                       "Left button state: %d, Right button state: %d",
                       &left, &right) == 2)
            {
                sem_wait(g_semGame);
                if (left == 1)  g_gamePtr->player[idx].x -= 15.0f;
                if (right == 1) g_gamePtr->player[idx].x += 15.0f;
                sem_post(g_semGame);
            }
            // 其它格式的字串就略過或做其它處理
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
        // 檢查遊戲結束
        sem_wait(g_semGame);
        bool over = g_gamePtr->gameOver;
        bool isAlive = g_gamePtr->player[idx].isAlive;

        // 建構 stateMsg
        json stateMsg;
        // stateMsg["game_state"] = (g_gamePtr->gameOver) ? 
        //                           "Game_over" : "Game_start";
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
            arr.push_back(pp);
        }
        stateMsg["players"]   = arr;
        stateMsg["bullets"]   = json::parse(g_gamePtr->bulletsJSON);
        stateMsg["obstacles"] = json::parse(g_gamePtr->obstaclesJSON);
        sem_post(g_semGame);

        // 傳送給 client
        if (!sendJsonToClient(fd, stateMsg)) {
            break;
        }

        // 如果玩家已死，離開
        // if (!isAlive) break;
        // 如果遊戲結束，離開
        if (over) break;

        usleep(200000); // 0.2s 間隔
    }

    std::cout << "[Player" << (idx+1) 
              << "] Thread2 (Send) end.\n";
    return nullptr;
}

// =========== 改造後的 runPlayerThread ===========

void* runPlayerThread(void* threadArg)
{
    // 提取參數
    int playerIdx = *(int*)threadArg; 
    int fd        = g_playerFd[playerIdx];

    std::cout << "[PlayerThread] Player" << (playerIdx+1) 
              << " started.\n";

    // [ADD] Ready 狀態：傳送 JSON 給該玩家，告知他是第幾號玩家
    {
        sem_wait(g_semGame);

        json readyMsg;
        readyMsg["game_state"] = "Ready";

        // 只包含當前玩家自己的資訊
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

        // 送給 client
        sendJsonToClient(fd, readyMsg);
    }
    
    // 等待大家就緒
    std::cout << "game set for player " << playerIdx << std::endl;
    sleep(8);
    pthread_barrier_wait(&g_barrier);
    std::cout << "game started for player " << playerIdx << std::endl;

    // 建立兩條子threads
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

    // 送給 client
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

    // 初始化 pthread_barrier，等 2 個 player + 1 個遊戲
    pthread_barrier_init(&g_barrier, nullptr, MAX_PLAYERS + 1);

    // 建立 Shared Memory
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
    g_gamePtr->gameRemainingTime  = 30;
    g_gamePtr->bulletTimerCount   = 0;
    g_gamePtr->obstacleTimerCount = 0;

    // 設定初始玩家位置
    g_gamePtr->player[0] = {250.0f, 550.0f, true, 0};
    g_gamePtr->player[1] = {500.0f, 550.0f, true, 0};

    strcpy(g_gamePtr->bulletsJSON, "[]");
    strcpy(g_gamePtr->obstaclesJSON, "[]");

    // 建立 semaphore
    sem_unlink("/gameSem");
    g_semGame = sem_open("/gameSem", O_CREAT, 0666, 1);
    if (g_semGame == SEM_FAILED) {
        std::cerr << "[Main] sem_open failed\n";
        return 1;
    }

    // 創建遊戲執行緒
    if (pthread_create(&g_gameThread, nullptr, runGameThread, nullptr) != 0) {
        std::cerr << "[Main] pthread_create for GameThread failed\n";
        return 1;
    }

    // 建立 socket
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

    // 接受 2 位玩家
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

        // 建立玩家執行緒
        //  這裡用一個小 trick：將 playerIdx 存在陣列裡，以便傳給執行緒函數
        static int playerIndices[MAX_PLAYERS] = {0,1};
        if (pthread_create(&g_playerThread[i], nullptr,
                           runPlayerThread,
                           &playerIndices[i]) != 0) 
        {
            std::cerr << "[Main] pthread_create for PlayerThread failed\n";
            return 1;
        }
    }

    // Main Thread：輪詢遊戲結束 (可改用 join gameThread 方式)
    while (true) {
        sem_wait(g_semGame);
        bool over = g_gamePtr->gameOver;
        sem_post(g_semGame);
        if (over) break;
        sleep(1);
    }

    std::cout << "[Main] GameOver, Cleaning up...\n";

    // 若需要確保 GameThread 與 PlayerThreads 結束，可在此 pthread_join
    pthread_join(g_gameThread, nullptr);
    for (int i = 0; i < MAX_PLAYERS; i++){
        pthread_join(g_playerThread[i], nullptr);
    }

    // 關閉
    close(g_serverFd);
    shmdt(g_gamePtr);
    shmctl(g_shmid, IPC_RMID, NULL);
    sem_close(g_semGame);
    sem_unlink("/gameSem");

    pthread_barrier_destroy(&g_barrier);
    std::cout << "[Main] End.\n";
    return 0;
}