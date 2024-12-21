#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define DEVICE "/dev/etx_device"  // 設備文件路徑

int main() {
    int fd;
    char buffer[2];  // 用來存放按鈕狀態

    // 打開設備文件
    fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device");
        return 1;
    }

    printf("Monitoring button states...\n");

    while (1) {
        // 讀取驅動中的按鈕狀態
        if (read(fd, buffer, sizeof(buffer)) < 0) {
            perror("Failed to read from the device");
            close(fd);
            return 1;
        }

        int left_button_state = buffer[0];  // 左按鈕狀態 (0或1)
        int right_button_state = buffer[1]; // 右按鈕狀態 (0或1)

        // 按一次就印一次，如果一直按著就持續印
        if (left_button_state) {
            printf("Left button pressed!\n");
            fflush(stdout);
            usleep(100000); // 等待 100ms 避免過度頻繁輸出，可自行調整
        }

        if (right_button_state) {
            printf("Right button pressed!\n");
            fflush(stdout);
            usleep(100000); // 同上
        }

        // 若無按鍵被按下，保持靜默並稍作延遲，減少CPU占用
        if (!left_button_state && !right_button_state) {
            usleep(100000);
        }
    }

    close(fd);
    return 0;
}
