#include "webrtc_connection.h"
#include "message_queue.h"
#include "log.h"

#include "base/logging.h"
#include "observer.h"
#include "platform_socket.h"

#include "base/ssladapter.h"
#include "base/physicalsocketserver.h"

#include "http.h"

#define MAX_SIGNALING_MSG_LEN   (8 * 1024)
#define MAX_CONNECTION_TIME_SEC (60)


WebrtcConnection::WebrtcConnection(uint32_t peerId):
    m_peerId(peerId),
    m_clientSocket(nullptr),
	m_queue(),
    m_state(CONNECTION_STATE_INIT),
	m_thread(rtc::Thread::Create()),
	m_mutex(),
    m_alive(true)	
{
}

WebrtcConnection::~WebrtcConnection() {
}


ConnectionState_t WebrtcConnection::getState() {
	return m_state;
}

uint32_t WebrtcConnection::getPeerId() {
    return m_peerId;
}

void WebrtcConnection::pushMessage(std::string& message) {
	m_queue.enqueue(message);
}

int32_t WebrtcConnection::connect() {
	m_thread.get()->Start(this);
	return 0;
}

void WebrtcConnection::setOutboundSocket(TcpSocket* clientSocket) {
	std::unique_lock<std::mutex> lock(m_mutex);
	m_clientSocket = clientSocket;
}

int32_t WebrtcConnection::disconnect() {
	m_alive = false;
	m_thread.get()->Stop();
	return 0;
}

void WebrtcConnection::sendMessage(std::string& message) {
	logWrite(HTTP_LOG_DEBUG, "sendMessage +"); 
	char httpHeader[HTTP_HEADER_SIZE_MAX];

	size_t dataSize = strlen(message.c_str());
	snprintf(httpHeader, HTTP_HEADER_SIZE_MAX -1 , "HTTP/1.1 200 OK\r\n" \
                             "Connection: close\r\n" \
							 "Server: %s\r\n" \
                             "Content-Type: text/plain\r\n" \
                             "Accept-Ranges: none\r\n" \
							 "Pragma: %u\r\n" \
                             "Cache-Control: no-store\r\n" \
                             "Content-Length: %d\r\n\r\n",							 
							 HTTP_SERVER_NAME, m_peerId, dataSize);
    m_clientSocket->write(httpHeader, strlen(httpHeader));
    if (dataSize > 0)
        m_clientSocket->write(message.c_str(), dataSize);
	logWrite(HTTP_LOG_DEBUG, "sendMessage -");
}



void WebrtcConnection::Run(rtc::Thread* thread) {
	m_state = CONNECTION_STATE_CONNECTED;
	logWrite(HTTP_LOG_DEBUG, "Starting Peer Connection(%u)", m_peerId);

	std::string message;
	rtc::scoped_refptr<Observer> observer(new rtc::RefCountedObject<Observer>(m_peerId));
	//logWrite(HTTP_LOG_DEBUG, "Observer peer id: %u", observer.get()->getPeerId());
	std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
	while (m_alive) {
		if (m_queue.dequeue(message) != -1) {	
			logWrite(HTTP_LOG_DEBUG, "Message -->> %s", message.c_str());
			observer.get()->onSignalingMessage(0, message);
		}
		
		if (observer.get()->getSignalingMessageCount() > 0) {
			std::unique_lock<std::mutex> lock(m_mutex);
			if (m_clientSocket != nullptr) {
				if (observer.get()->getSignalingMessage(message) == 0) {
					logWrite(HTTP_LOG_DEBUG, "<<-- Message %s", message.c_str());			
					sendMessage(message);
					delete m_clientSocket;
					m_clientSocket = nullptr;
				}
			} else {
				//logWrite(HTTP_LOG_DEBUG, "m_clientSocket is null");	
			}
		}
		webrtc::PeerConnectionInterface::IceConnectionState localIceState = observer.get()->getIceState();
		if (localIceState == webrtc::PeerConnectionInterface::kIceConnectionDisconnected) {
			m_alive = false;
		} else if (localIceState != webrtc::PeerConnectionInterface::kIceConnectionConnected) {
			std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
			int64_t diff = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
			if (diff >= MAX_CONNECTION_TIME_SEC) {
				m_alive = false;
				logWrite(HTTP_LOG_DEBUG, "Peer(%u) not connected in %d seconds, aborting", 
				                         observer.get()->getPeerId(), MAX_CONNECTION_TIME_SEC);
			}
		}
		thread->ProcessMessages(100);
	}
	logWrite(HTTP_LOG_DEBUG, "Ending Peer Connection(%u)", m_peerId);
	m_state = CONNECTION_STATE_END;
}