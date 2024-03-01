#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>

int main() {

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(16999);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

    listen(serverSocket, 5);

    int clientSocket = accept(serverSocket, nullptr, nullptr);

    std::cout << "accepted client" << std::endl;

    const std::string msg = "hihi";
    send(clientSocket, msg.c_str(), msg.size(), 0);

    std::cout << "sent message" << std::endl;

    shutdown(serverSocket, 0);
    shutdown(clientSocket, 0);
}