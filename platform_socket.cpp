#include "platform_socket.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <Ws2tcpip.h>

       
SOCKET createSocket(int type, int protocol) {
    WSADATA wsaData = {0};
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        logWrite(HTTP_LOG_DEBUG, "WSAStartup() failed: %d", iResult);
        return INVALID_SOCKET;
    }
    SOCKET _fd = socket(AF_INET, type, protocol);
    if (_fd == INVALID_SOCKET) {
        logWrite(HTTP_LOG_DEBUG, "socket() failed, error: %d", WSAGetLastError());
    }    
    return _fd;
}


Socket::Socket(const std::string& ip, uint16_t port, SSL_CTX* ctx):
    m_sslCtx(ctx),
	m_ssl(nullptr),
    m_handle(0),
	m_ip(ip),
	m_port(port) {
}

Socket::~Socket() {
	if (m_ssl != nullptr) {
        SSL_free(m_ssl);   
	}
	if (m_handle != 0)
		closesocket(m_handle);
}

int32_t Socket::bind() {
    struct sockaddr_in addr; 
    memset(&addr, '0', sizeof(addr));

    addr.sin_family = AF_INET;
    if (m_ip == "any")
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else {
        addr.sin_addr.s_addr = inet_addr(m_ip.c_str());
        //inet_pton(AF_INET, ipAddr, &addr.sin_addr);
    }
    addr.sin_port = htons(m_port); 

    if (::bind(m_handle, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        logWrite(HTTP_LOG_DEBUG, "bind() failed, error: %d, ip: %s, port: %d", WSAGetLastError(), m_ip.c_str(), m_port);
        return -1;
    }
	return 0;
}

int32_t Socket::read(char* buffer, int32_t size) {
    int32_t ret = 0;
	
    fd_set readFds;
    TIMEVAL timeout;
    
    timeout.tv_sec = 0;
    timeout.tv_usec = SOCKET_READ_TIMEOUT;
    
    FD_ZERO(&readFds);
    FD_SET(m_handle, &readFds);
    ret = select (0, &readFds, NULL, NULL, &timeout);
    if (ret == SOCKET_ERROR) {
        logWrite(HTTP_LOG_DEBUG, "select() failed, error: %d", WSAGetLastError());
        return -1;
    }
    
    if (!FD_ISSET(m_handle, &readFds)) {
		//logWrite(HTTP_LOG_DEBUG, "Socket::read() timeout");
        return 0;
    }
	
	if (m_sslCtx != nullptr && m_ssl != nullptr) {
		ret = SSL_read(m_ssl, buffer, size);
	} else {
		ret = recv(m_handle, buffer, size, 0);
	}
    if (ret == -1) {
        //logWrite(HTTP_LOG_DEBUG, "recv() failed, error: %d", WSAGetLastError());
        return -1;
    }
    return ret;
}

int32_t Socket::write(const char* buffer, int32_t size) {
    int32_t ret = 0;
	if (m_sslCtx != nullptr && m_ssl != nullptr) {
		ret = SSL_write(m_ssl, buffer, size);
	} else {
		ret = send(m_handle, (const char*)buffer, size, 0);
	}
    if (ret == -1) {
        logWrite(HTTP_LOG_DEBUG, "send() failed, error: %d", WSAGetLastError());
        return -1;
    }
    return ret;
}

	
std::string Socket::getLocalIp() {
    struct sockaddr_in addr;
    int len = sizeof(addr);
    if (getsockname(m_handle, (struct sockaddr*)&addr, &len) != 0) {
        logWrite(HTTP_LOG_DEBUG, "getsockname() failed, error: %d", WSAGetLastError());
        assert(0);
    }
	char ipAddr[16];
    uint32_t ip = ntohl(addr.sin_addr.s_addr);
    uint8_t* ptr = (uint8_t*)&ip;
    sprintf(ipAddr, "%d.%d.%d.%d", ptr[3], ptr[2], ptr[1], ptr[0]); 
	
	return std::string(ipAddr);
}

std::string Socket::getPeerIp() {
    struct sockaddr_in addr;
    int len = sizeof(addr);
    if (getpeername(m_handle, (struct sockaddr*)&addr, &len) != 0) {
        logWrite(HTTP_LOG_DEBUG, "getsockname() failed, error: %d", WSAGetLastError());
        assert(0);
    }
	char ipAddr[16];
    uint32_t ip = ntohl(addr.sin_addr.s_addr);
    uint8_t* ptr = (uint8_t*)&ip;
    sprintf(ipAddr, "%d.%d.%d.%d", ptr[3], ptr[2], ptr[1], ptr[0]); 
	
	return std::string(ipAddr);
}


TcpSocket::TcpSocket(const std::string& ip, uint16_t port, SSL_CTX* ctx):
    Socket(ip, port, ctx) {
}

TcpSocket::~TcpSocket() {
}

int32_t TcpSocket::create() {
	assert(m_handle == 0 && !m_ip.empty() && m_port != 0);
	
    SOCKET _fd = createSocket(SOCK_STREAM, IPPROTO_TCP);
	if (_fd == INVALID_SOCKET)
	    return -1;
    int enable = 1;
    if (setsockopt(_fd, 
                   SOL_SOCKET, 
                   SO_REUSEADDR, 
                   (const char*)&enable, sizeof(enable)) == -1) 
    {
        logWrite(HTTP_LOG_DEBUG, "setsockopt(SO_REUSEADDR) failed, error: %d", WSAGetLastError());
		return -1;
    }
	
    DWORD val = 1;
    if (setsockopt(_fd, IPPROTO_IP, IP_DONTFRAGMENT, (char *)&val, sizeof(val)) == -1) {
		logWrite(HTTP_LOG_DEBUG, "setsockopt(IP_DONTFRAGMENT) failed, error: %d", WSAGetLastError());
	}

	
    m_handle = _fd;
	return 0;
}

int32_t TcpSocket::listen(int backlog) {
    if (::listen(m_handle, backlog) == SOCKET_ERROR) {
        logWrite(HTTP_LOG_DEBUG, "listen() failed, error: %d", WSAGetLastError());
        return -1;
    }
	return 0;
}

TcpSocket* TcpSocket::accept() {
    fd_set readFds;
    TIMEVAL timeout;
    
    timeout.tv_sec = SOCKET_CONNECTION_TIMEOUT;
    timeout.tv_usec = 0;
    
    FD_ZERO(&readFds);
    FD_SET(m_handle, &readFds);
    int ret = select (0, &readFds, NULL, NULL, &timeout);
    if (ret == SOCKET_ERROR) {
        logWrite(HTTP_LOG_DEBUG, "select() failed, error: %d", WSAGetLastError());
        return nullptr;
    }
    
    if (!FD_ISSET(m_handle, &readFds)) {
        return nullptr;
    }
    
    struct sockaddr_in addr;
    int len = sizeof(addr);
    SOCKET clientFd = 0;
    if ((clientFd = ::accept(m_handle, 
                           (struct sockaddr*)&addr, (int*)&len)) == SOCKET_ERROR) {
        logWrite(HTTP_LOG_DEBUG, "accept() failed, error: %d", WSAGetLastError());
        return nullptr;
    } 
	
    DWORD val = 1;
    if (setsockopt(clientFd, IPPROTO_IP, IP_DONTFRAGMENT, (char *)&val, sizeof(val)) == -1) {
		logWrite(HTTP_LOG_DEBUG, "setsockopt(IP_DONTFRAGMENT) failed, error: %d", WSAGetLastError());
	}
	
	SSL* ssl = nullptr;
	if (m_sslCtx != nullptr) {
		ssl = SSL_new(m_sslCtx);   
		if (ssl == nullptr) {
			logWrite(HTTP_LOG_ERROR, "SSL_new failed");
            return nullptr;
		}
        SSL_set_fd(ssl, clientFd); 
		if (SSL_accept(ssl) == -1) {
			//logWrite(HTTP_LOG_ERROR, "SSL_accept failed");
            return nullptr;
		}
	}
	TcpSocket* t = new TcpSocket(std::string(), 0, m_sslCtx);
    t->m_handle = clientFd;    
	t->m_ssl = ssl;
 
    return t;
}

int32_t TcpSocket::connect(const std::string& ip, uint16_t port) {
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons(port);
    if (::connect(m_handle, (SOCKADDR *) & addr, sizeof (addr)) == SOCKET_ERROR) {
		logWrite(HTTP_LOG_DEBUG, "connect(%s, %d) failed, error: %d",
                      		     ip.c_str(), port, WSAGetLastError());
		return -1;
    }
	return 0;
}
