#ifndef WEBRTC_CONNECTION_H
#define WEBRTC_CONNECTION_H

#include "base/thread.h"
#include "message_queue.h"

#include "platform_socket.h"

typedef enum {
    CONNECTION_STATE_INIT       = 1,
	CONNECTION_STATE_CONNECTED  = 2,
	CONNECTION_STATE_ERROR      = 3,
	CONNECTION_STATE_END        = 4,
} ConnectionState_t;

class WebrtcConnection: public rtc::Runnable {
public:
    WebrtcConnection(uint32_t peerId);
	virtual ~WebrtcConnection();
	ConnectionState_t getState();
	int32_t connect();
	int32_t disconnect();
	void setOutboundSocket(TcpSocket* clientSocket);
	uint32_t getPeerId();
	void pushMessage(std::string& message);
	
	virtual void Run(rtc::Thread* thread);
	
private:
    void sendMessage(std::string& message);
	
private:
    uint32_t          m_peerId;
    TcpSocket*        m_clientSocket;
	MessageQueue      m_queue;
	ConnectionState_t m_state;
	std::unique_ptr<rtc::Thread> m_thread;
	std::mutex        m_mutex;
	bool              m_alive;
};

#endif //WEBRTC_CONNECTION_H