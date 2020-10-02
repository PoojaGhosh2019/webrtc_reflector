#ifndef PLATFORM_SOCKET_H
#define PLATFORM_SOCKET_H

#include <openssl/ssl.h>

#include <winsock2.h>
#include <iostream>
#include <assert.h>
#include <windows.h>
#include <Sysinfoapi.h>

#define SOCKET_READ_TIMEOUT       (200 * 1000)
#define SOCKET_CONNECTION_TIMEOUT (10)
#define MULTICAST_TTL             (128)

class Socket {
public: 
    Socket(const std::string& ip, uint16_t port, SSL_CTX* ctx = nullptr);
	virtual ~Socket();
	virtual int32_t create() = 0;
	int32_t bind();
	int32_t read(char* buffer, int32_t size);
	int32_t write(const char* buffer, int32_t size);	
	std::string getLocalIp();
	std::string getPeerIp();
protected:
    SSL_CTX*        m_sslCtx;
	SSL*            m_ssl;
    SOCKET          m_handle;
	std::string     m_ip;
	uint16_t        m_port;
};

class TcpSocket: public Socket {
public: 
    TcpSocket(const std::string& ip, uint16_t port, SSL_CTX* ctx = nullptr);
	virtual ~TcpSocket();
	virtual int32_t create();
    int32_t listen(int backlog);
    TcpSocket* accept();
	int32_t connect(const std::string& ip, uint16_t port);
	SOCKET getFd() { return m_handle; }
};

#endif // PLATFORM_SOCKET_H
