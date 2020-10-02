#include <iostream>
#include <fstream>
#include <sstream>

#include <openssl/ssl.h>

#include "getopt.h"
#include "platform_socket.h"
#include "base/ssladapter.h"
#include "log.h"
#include "webrtc_connection.h"
#include "http.h"

#define HTTP_RW_BUFFER_SIZE  (32 * 1024)

#define HTTP_CMD_LANDING_PAGE "index.html"
#define HTTP_CMD_SIGN_IN      "signIn"
#define HTTP_CMD_SIGN_OUT     "signOut"
#define HTTP_CMD_MESSAGE      "message"
#define HTTP_CMD_WAIT         "wait"
#define HTTP_CMD_FAVICON      "favicon.ico"

extern int32_t g_log;

#define CERTIFICATE_FILE      "server.pem"

static std::string g_serverIp = "";
static uint16_t g_httpsPort = 4433;
static std::string g_certificate = CERTIFICATE_FILE;


void sendReplyError(TcpSocket* socket, std::string errorTxt, HttpReq_t& httpReq, char* outBuffer) {
	static char errorTemplate[] = "<!DOCTYPE html>\n \
                 <html>\n \
                 <body>\n \
                 <h1>%s</h1>\n \
                 </body>\n \
                 </html>";
	char errorPage[128];
	snprintf(errorPage, 127, errorTemplate, errorTxt.c_str());
	snprintf(outBuffer, 1024 -1 , "HTTP/1.1 200 OK\r\n" \
                             "Connection: close\r\n" \
							 "Server: %s\r\n" \
                             "Content-Type: text/html; charset=UTF-8\r\n" \
                             "Accept-Ranges: none\r\n" \
                             "Content-Encoding: UTF-8\r\n" \
                             "Cache-Control: no-store\r\n" \
                             "Content-Length: %d\r\n\r\n", "myserver", strlen(errorPage));
	socket->write(outBuffer, strlen(outBuffer));
	socket->write(errorPage, strlen(errorPage));
}


void sendReplyPage(TcpSocket* socket, std::string fileName, const char* mime, HttpReq_t& httpReq, char* outBuffer) {
	std::ifstream fileIn(fileName);
    if(!fileIn.is_open()) {
		logWrite(HTTP_LOG_ERROR, "Unable to open file = %s, error = %d", fileName.c_str(), errno);  
		sendReplyError(socket, "Resource not found!", httpReq, outBuffer);
		return;
    }
    std::ostringstream ss;
    ss << fileIn.rdbuf(); // reading data
    int32_t pageSize = ss.str().size() + 64;
	char* page = new char[pageSize];
	snprintf(page, pageSize - 1, ss.str().c_str(), g_serverIp.c_str(), g_httpsPort, g_log);
	snprintf(outBuffer, HTTP_HEADER_SIZE_MAX -1 , "HTTP/1.1 200 OK\r\n" \
                             "Connection: close\r\n" \
							 "Server: %s\r\n" \
                             "Content-Type: %s\r\n" \
							 "Accept-Ranges: none\r\n" \
                             "Cache-Control: no-store\r\n" \
                             "Content-Length: %d\r\n\r\n", 
							 HTTP_SERVER_NAME, mime, strlen(page));
	socket->write(outBuffer, strlen(outBuffer));
    socket->write(page, strlen(page));
	delete page;
	fileIn.close();
}


void sendReplyFavicon(TcpSocket* socket, std::string fileName, const char* mime, HttpReq_t& httpReq, char* outBuffer) {
	FILE* file = fopen(fileName.c_str(), "rb");
	if (file == nullptr) {
		logWrite(HTTP_LOG_ERROR, "Unable to open file = %s, error = %d", fileName.c_str(), errno);  
		sendReplyError(socket, "Resource not found!", httpReq, outBuffer);
		return;
	}
    struct stat fileStat;
    if (stat(fileName.c_str(), &fileStat) == -1) {
		logWrite(HTTP_LOG_ERROR, "Unable to stat file = %s, error = %d", fileName.c_str(), errno);  
		sendReplyError(socket, "Resource not found!", httpReq, outBuffer);
		return;
    }
    if (fileStat.st_size <= 0) {
		logWrite(HTTP_LOG_ERROR, "File size invalid = %s, error = %d", fileName.c_str(), errno);  
		sendReplyError(socket, "Resource not found!", httpReq, outBuffer);
		return;
    }
	
    uint8_t buffer[4096];
	uint8_t* fileContent = new uint8_t[fileStat.st_size];
	int32_t bytes = 0;
	int32_t totalBytes = 0;
	while ((bytes = fread((void*)buffer, 4096, 1, file)) > 0) {
		memcpy (fileContent + totalBytes, buffer, bytes);
		totalBytes += bytes;
	}
	
	snprintf(outBuffer, HTTP_HEADER_SIZE_MAX -1 , "HTTP/1.1 200 OK\r\n" \
                             "Connection: close\r\n" \
							 "Server: %s\r\n" \
                             "Content-Type: %s\r\n" \
							 "Accept-Ranges: none\r\n" \
                             "Cache-Control: no-store\r\n" \
                             "Content-Length: %d\r\n\r\n", 
							 HTTP_SERVER_NAME, mime, totalBytes);
	socket->write(outBuffer, strlen(outBuffer));
    socket->write((const char*)fileContent, totalBytes);
	delete fileContent;
	fclose(file);
}


void sendReply(TcpSocket* socket, HttpReq_t& httpReq, uint8_t* data, uint32_t dataSize, char* outBuffer) {
	snprintf(outBuffer, HTTP_HEADER_SIZE_MAX -1 , "HTTP/1.1 200 OK\r\n" \
                             "Connection: close\r\n" \
							 "Server: %s\r\n" \
							 "Origin: %s\r\n" \
                             "Content-Type: text/plain\r\n" \
                             "Accept-Ranges: none\r\n" \
							 "Pragma: %u\r\n" \
                             "Cache-Control: no-store\r\n" \
                             "Content-Length: %d\r\n\r\n",							 
							 HTTP_SERVER_NAME, httpReq.origin.c_str(), httpReq.pragma, dataSize);
							 
	socket->write(outBuffer, strlen(outBuffer));
	if (dataSize > 0)
		socket->write((const char*)data, dataSize);
}

void printUsage() {
    printf("Usage: server.exe -i ip -p port [-c certificate]\n");
    printf("\t -i ip\t\tWebserver IP\n");
    printf("\t -p port\tWebserver port\n");
	printf("\t -c certificate\tFull path of the certificate(.pem file) for SSL\n");
}

typedef std::vector<WebrtcConnection*> WebrtcConnectionList_t;
WebrtcConnectionList_t webrtcConnectionList;

int main(int argc, char** argv)
{
    if (argc < 3) {
		printUsage();
        exit(1);
    }
	
	int32_t opt = -1;
    while ((opt = getopt(argc, &argv[0], "i:p:c:")) != -1) {
        switch (opt) {
        case 'i': g_serverIp = optarg; break;
        case 'p': g_httpsPort = (uint16_t)atoi(optarg); break;
		case 'c': g_certificate = optarg; break;
        default:
            printUsage();
            exit(1);
        }
    }
	
	char* env = std::getenv("ENABLE_LOG");
	if (env != nullptr && atoi(env) > 0)
	    g_log = atoi(env);
	
	logWrite(HTTP_LOG_ERROR, "Staring %s @%s:%d", HTTP_SERVER_NAME, g_serverIp.c_str(), g_httpsPort);

    //rtc::LogMessage::LogToDebug(rtc::LS_INFO);
	rtc::InitializeSSL();
		
    SSL_CTX * ctx = SSL_CTX_new(TLS_server_method());
    if (!SSL_CTX_use_certificate_chain_file(ctx, g_certificate.c_str())) {
		logWrite(HTTP_LOG_ERROR, "SSL_CTX_use_certificate_chain_file failed for %s", g_certificate.c_str());
		exit(1);
	}
    if (!SSL_CTX_use_PrivateKey_file(ctx, g_certificate.c_str(), SSL_FILETYPE_PEM)) {
		logWrite(HTTP_LOG_ERROR, "SSL_CTX_use_PrivateKey_file failed for %s", g_certificate.c_str());
		exit(1);
	}
    if (!SSL_CTX_check_private_key(ctx)) {
		logWrite(HTTP_LOG_ERROR, "SSL_CTX_check_private_key failed");
		exit(1);
	}
	
    TcpSocket* serverSocket = new TcpSocket(g_serverIp.c_str(), g_httpsPort, ctx);
	serverSocket->create();
	serverSocket->bind();
	
	serverSocket->listen(20);
    TcpSocket* clientSocket = nullptr; 
	char* httpRwBuffer = new char[HTTP_RW_BUFFER_SIZE];
	uint32_t peerId = 1;
	bool closeWait = true;
	while (1) {
		if ((clientSocket = serverSocket->accept()) == nullptr) {
			//Cleanup dead clients
			WebrtcConnectionList_t::iterator it = webrtcConnectionList.begin();
			while (it != webrtcConnectionList.end()) {
				WebrtcConnection* connection = *it;
				if (connection->getState() == CONNECTION_STATE_END) {
					logWrite(HTTP_LOG_DEBUG, "Cleanup dead peer connection(%u)", connection->getPeerId());
					connection->disconnect();
					it = webrtcConnectionList.erase(it);
					delete connection;				
				} else { 
					it++;
				}
			}
            continue;
        }
		
		int32_t bytes = 0;
		int32_t totalBytes = 0;
		HttpReq_t httpReq;
		bool parsingDone = false;
		while ((bytes = clientSocket->read(httpRwBuffer + totalBytes, HTTP_RW_BUFFER_SIZE - totalBytes)) > 0) {
			totalBytes += bytes;
		}
		httpRwBuffer[totalBytes] = '\0';
		parsingDone = parseHttpMessage(httpRwBuffer, totalBytes, httpReq);		
		if (!parsingDone) {
			logWrite(HTTP_LOG_DEBUG, "Unable to parse HTTP message");
			delete clientSocket;
			continue;
		}
		
		if(httpReq.url.cmd.empty() ||
		   httpReq.url.cmd == "" ||
		   httpReq.url.cmd == HTTP_CMD_LANDING_PAGE) {
			logWrite(HTTP_LOG_DEBUG, "Request: Landing page");
			sendReplyPage(clientSocket, HTTP_CMD_LANDING_PAGE, "text/html; charset=UTF-8", httpReq, httpRwBuffer);
		} else if (httpReq.url.cmd == HTTP_CMD_SIGN_IN) {
			logWrite(HTTP_LOG_DEBUG, "Request: signIn");
			WebrtcConnection* connection = new WebrtcConnection(peerId++);
			webrtcConnectionList.push_back(connection);
			connection->connect();
			httpReq.pragma = connection->getPeerId();
			sendReply(clientSocket, httpReq, nullptr, 0, httpRwBuffer);
		} else if (httpReq.url.cmd == HTTP_CMD_SIGN_OUT) {
			logWrite(HTTP_LOG_DEBUG, "Request: signOut(%d)", httpReq.pragma);
		} else if (httpReq.url.cmd == HTTP_CMD_MESSAGE) {
			logWrite(HTTP_LOG_DEBUG, "Request: message(%d)", httpReq.pragma);
			WebrtcConnectionList_t::iterator it = webrtcConnectionList.begin();
			while (it != webrtcConnectionList.end()) {
				WebrtcConnection* connection = *it;
				if (connection->getPeerId() == httpReq.pragma) {
					logWrite(HTTP_LOG_DEBUG, "Media payload size = %u", httpReq.dataSize);
					std::string message = (char*)httpReq.data;
					connection->pushMessage(message);
					//logWrite(HTTP_LOG_DEBUG, "Media payload = %s", message.c_str());
					break;
				} 
				it++;
			}
			sendReply(clientSocket, httpReq, nullptr, 0, httpRwBuffer);	
		} else if (httpReq.url.cmd == HTTP_CMD_WAIT) {
			logWrite(HTTP_LOG_DEBUG, "Request: wait(%d)", httpReq.pragma);
			WebrtcConnectionList_t::iterator it = webrtcConnectionList.begin();
			while (it != webrtcConnectionList.end()) {
				WebrtcConnection* connection = *it;
				if (connection->getPeerId() == httpReq.pragma) {
					logWrite(HTTP_LOG_DEBUG, "Updating out bound socket for %d", httpReq.pragma);
					connection->setOutboundSocket(clientSocket);
					closeWait = false;
					break;
				} 
				it++;
			}
		} else if (httpReq.url.cmd == HTTP_CMD_FAVICON) {
			logWrite(HTTP_LOG_DEBUG, "Request: favicon.ico");
			sendReplyFavicon(clientSocket, HTTP_CMD_FAVICON, "image/x-icon", httpReq, httpRwBuffer);
		} else {
			sendReplyError(clientSocket, "Invalid Request!", httpReq, httpRwBuffer);
		}
		
		if (closeWait)
			delete clientSocket;
	}
	
	delete httpRwBuffer;
			
    return 0;
}

