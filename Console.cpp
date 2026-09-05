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

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5555);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Console: Failed to connect to CoreRelay." << endl;
        return 1;
    }

    cout << "Developer Console connected. Type commands (e.g. FORCE_CLOSE 5) or 'exit' to quit." << endl;

    // Console expects manual input from stdin for testing
    string ciste_valsi;
    while (cin >> ciste_valsi) {
        if (ciste_valsi == "exit") {
            break;
        } else if (ciste_valsi == "FORCE_CLOSE" || ciste_valsi == "ADMIN_CLOSE") {
            int target_dock;
            cin >> target_dock;
            string command = "TARGET:" + to_string(target_dock) + "|PAYLOAD:ADMIN_CLOSE\n";
            send(sock, command.c_str(), command.length(), 0);
            cout << "Console: Sent ADMIN_CLOSE to Dock " << target_dock << endl;
        }
    }

    closesocket(sock);
    return 0;
}
