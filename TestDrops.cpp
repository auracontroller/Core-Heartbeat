#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

using namespace std;

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    cout << "TestDrops starting." << endl;
    for (int i = 0; i < 50; ++i) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(5555);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            cerr << "TestDrops: Failed to connect on iteration " << i << endl;
            continue;
        }

        // Abruptly close it immediately
        // By turning on linger with 0 timeout, close() sends a RST instead of FIN
        struct linger sl;
        sl.l_onoff = 1;
        sl.l_linger = 0;
        setsockopt(sock, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));

        closesocket(sock);
    }

    cout << "TestDrops completed rapid connections and abrupt drops." << endl;

    return 0;
}