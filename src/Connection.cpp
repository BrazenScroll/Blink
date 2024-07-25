#include "Connection.h"

Connection::Connection() {
    _socket = socket(AF_INET, SOCK_STREAM, 0);
    if(_socket == -1) {
        throw std::runtime_error("Could not create socket");
    }

	if (sodium_init() < 0) {
		throw std::runtime_error("Could not initialize sodium");
	}

	crypto_box_keypair(_keyPair.publicKey, _keyPair.secretKey);
}

void Connection::connectToServer(std::string ip, int port) {
    _server.sin_family = AF_INET;
    _server.sin_port = htons(port);

    if(ip == "localhost" || ip.empty())
        ip = "127.0.0.1";


    if(inet_pton(AF_INET, ip.c_str(), &_server.sin_addr) <= 0) {
        if (auto result = dnsLookup(ip); result.empty()) {
            throw std::runtime_error("Invalid address / Address not supported");
        } else {
            ip = result[0];
            if(inet_pton(AF_INET, ip.c_str(), &_server.sin_addr) <= 0) {
                throw std::runtime_error("Invalid address / Address not supported");
            }
        }
    }

    connect(_socket, (struct sockaddr*)&_server, sizeof(_server));



}

void Connection::_send(const char * message, size_t length) {
	std::lock_guard<std::mutex> lock(_sendMutex);
	if(::send(_socket, message, length, 0) < 0) {
		throw std::runtime_error("Could not send message");
	}
}

void Connection::send(const std::string & message){
    auto messageToSend = message;

	if(_encrypted)
		_secretSeal(messageToSend);

	messageToSend += _end;

	_send(messageToSend.c_str(), messageToSend.size());
}

void Connection::sendMessage(const std::string & message) {
    send(_text + message);
}

void Connection::sendInternal(const std::string &message) {
	send(_internal + message);
}

std::string Connection::receive() {
    std::string message;


    while (!message.contains(_end)) {

        clearBuffer();

        _sizeOfPreviousMessage = recv(_socket, _buffer, 4096, 0);

        if(_sizeOfPreviousMessage < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != MSG_WAITALL) {
            throw std::runtime_error("Could not receive message from server: " + std::string(strerror(errno)));
        }

        message += _buffer;
		//std::cout << "RECEIVE |  " << message << std::endl;
    }

	// remove the _end string
	message = message.substr(0, message.find(_end));

	if(_encrypted)
		_secretOpen(message);


	if(message.contains(_internal"publicKey:")){
		std::string publicKey = message.substr(strlen(_internal"publicKey:"));

		if(sodium_base642bin(_remotePublicKey, crypto_box_PUBLICKEYBYTES, publicKey.c_str(), publicKey.size(), nullptr, nullptr, nullptr, sodium_base64_VARIANT_ORIGINAL) < 0){
			throw std::runtime_error("Could not decode public key");
		}

		auto pk_base64 = std::make_unique<char[]>(sodium_base64_encoded_len(crypto_box_PUBLICKEYBYTES, sodium_base64_VARIANT_ORIGINAL));
		auto pk_base64_len = sodium_base64_encoded_len(crypto_box_PUBLICKEYBYTES, sodium_base64_VARIANT_ORIGINAL);

		sodium_bin2base64(pk_base64.get(), pk_base64_len,
						  _keyPair.publicKey, crypto_box_PUBLICKEYBYTES, sodium_base64_VARIANT_ORIGINAL);

		//std::cout << "public key: " << pk_base64.get() << std::endl;

		send(_internal"publicKey:" + std::string(pk_base64.get(), pk_base64_len-1));

		_encrypted = true;
	}

    return message;
}

void Connection::close() {
    send(_internal"exit");
    shutdown(_socket, 0);
    _active = false;
}

Connection::~Connection() {
    if(_active)
        close();
}

void Connection::clearBuffer() {

	memset(_buffer, '\0', _sizeOfPreviousMessage);

}

std::vector<std::string> Connection::dnsLookup(const std::string & domain, int ipv) {
    // credit to http://www.zedwood.com/article/cpp-dns-lookup-ipv4-and-ipv6

    std::vector<std::string> output;

    struct addrinfo hints, *res, *p;
    int status, ai_family;
    char ip_address[INET6_ADDRSTRLEN];

    ai_family = ipv==6 ? AF_INET6 : AF_INET; //v4 vs v6?
    ai_family = ipv==0 ? AF_UNSPEC : ai_family; // AF_UNSPEC (any), or chosen
    memset(&hints, 0, sizeof hints);
    hints.ai_family = ai_family;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(domain.c_str(), NULL, &hints, &res)) != 0) {
        //cerr << "getaddrinfo: "<< gai_strerror(status) << endl;
        return output;
    }

    //cout << "DNS Lookup: " << host_name << " ipv:" << ipv << endl;

    for(p = res;p != NULL; p = p->ai_next) {
        void *addr;
        if (p->ai_family == AF_INET) { // IPv4
            auto *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
        } else { // IPv6
            auto *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
        }

        // convert the IP to a string
        inet_ntop(p->ai_family, addr, ip_address, sizeof ip_address);
        output.emplace_back(ip_address);
    }

    freeaddrinfo(res); // free the linked list

    return output;
}

void Connection::_secretSeal(std::string & message) {

	auto cypherText = std::make_unique<unsigned char[]>(crypto_box_SEALBYTES + message.size());

	if(crypto_box_seal(cypherText.get(), (unsigned char *)message.c_str(), message.size(), _remotePublicKey) < 0)
		throw std::runtime_error("Could not encrypt message");

	message = std::string((char *)cypherText.get(), crypto_box_SEALBYTES + message.size());
}

void Connection::_secretOpen(std::string & message) {

	auto cypherText_bin = std::make_unique<char[]>(message.size() - crypto_box_SEALBYTES);

	std::cout << "message: " << message << std::endl;

	if(crypto_box_seal_open((unsigned char *)cypherText_bin.get(), (unsigned char *)message.c_str(), message.size(), _keyPair.publicKey, _keyPair.secretKey) < 0)
		throw std::runtime_error("Could not decrypt message");

	message = std::string(cypherText_bin.get(), message.size() - crypto_box_SEALBYTES);
}