#include <winsock2.h>

#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <future>

#include <chrono>
#include <assert.h>

#include <string.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/applink.c>

#include <iostream>
#include <fstream>
#include <sstream>

#include <string.h>

#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>

#include "getopt.h"

#define HTTP_LINE_SIZE_MAX    (256)
#define HTTP_HEADER_SIZE_MAX  (1024)

#define HTTP_CMD_LANDING_PAGE "index.html"
#define HTTP_CMD_MEDIA_ECHO   "mediaEcho"
#define HTTP_CMD_FAVICON      "favicon.ico"

#define HTTP_SERVER_NAME      "WebRTC Reflector"

#define HTTP_MEDIA_PAYLOAD_MAX(x) (((x + 102399) / 102400) * 102400)

#define CERTIFICATE_FILE      "server.pem"

#define HTTP_LOG_ERROR        (0x01)
#define HTTP_LOG_DEBUG        (0x02)


typedef enum {
    CONNECTION_STATE_INIT = 1,
	CONNECTION_STATE_END  = 2,
} ConnectionState_t;

typedef struct {
	uint32_t          id;
	BIO*              sbio;
	std::thread*      thread;
	ConnectionState_t state;
} HttpConnction_t;

typedef std::vector<HttpConnction_t*> HttpConnctionList_t;

typedef struct {
	std::string key;
	std::string value;
} HttpUrlParam_t;

typedef struct {
	std::string cmd;
	std::vector<HttpUrlParam_t> param;
} HttpReqUrl_t;

typedef struct {
    uint32_t     sessionId;
    std::string  type;
    HttpReqUrl_t url;
    std::string  version;
	std::string  origin;
	bool         keepAlive;
	uint32_t     dataSize;
	uint32_t     fragNumber;
} HttpReq_t;


typedef struct {
    uint32_t start;
    uint32_t length;
} StringDesc_t;

typedef std::vector<StringDesc_t> StringDescList_t;


static std::string g_serverIp;
static uint16_t g_httpsPort = 4433;
static int32_t g_videoFragSize = 1;
static std::string g_certificate = CERTIFICATE_FILE;
static bool g_log = false;


int logWrite(uint8_t level, const char* fmt, ...) {
    if (level == HTTP_LOG_DEBUG && !g_log)
        return 0;
    va_list args;
    va_start (args, fmt);
    vprintf(fmt, args);
    va_end (args);
    printf("\n");
    return 0;
}

 
static size_t splitString(const char* str, 
                    uint32_t start, 
                    uint32_t end, std::string ptrn, StringDescList_t& list) {
	
    if (list.size() > 0)
        list.clear();
						
    uint32_t i = start;
    uint32_t s = i;
    while (i < end) {
        if (ptrn.find(str[i]) != std::string::npos) {
            if ((i - s) > 0) {
				StringDesc_t desc;
                desc.start = s;
                desc.length = i - s;
                list.push_back(desc);
            }
            s = i + 1;
        }
        i++;
    }
    if ((i - s) > 0) {
		StringDesc_t desc;
        desc.start = s;
        desc.length = i - s;
        list.push_back(desc);
    }
    
    return list.size();
}


static void copyString (const char* src, 
                 StringDesc_t& desc, std::string& dest) 
{
	dest.assign(src + desc.start, desc.length);
}


static int32_t parseUrl(std::string& buff, HttpReqUrl_t& reqUrl) {
    StringDescList_t strDesc;
    size_t count = splitString(buff.c_str(), 0, strlen(buff.c_str()), "/", strDesc);
	int32_t index = strDesc.size() - 1;
	if (index < 0)
		return -1;
    count = splitString(buff.c_str(), 
                        strDesc.at(index).start, 
                        strDesc.at(index).start + strDesc.at(index).length, "?", strDesc);
    copyString (buff.c_str(), strDesc.at(0), reqUrl.cmd);
	
    if (count > 1) {
        count = splitString(buff.c_str(), 
                        strDesc.at(1).start, 
                        strDesc.at(1).start + strDesc.at(1).length, "&", strDesc);
        uint32_t i;
        StringDescList_t strDesc2;
		std::string temp;
        for (i = 0; i < count; i++) {
            uint32_t count2 = splitString(buff.c_str(), 
                            strDesc.at(i).start, 
                            strDesc.at(i).start + strDesc.at(i).length, "=", strDesc2);
            if (count2 < 2)
                continue;
			
			HttpUrlParam_t param;
            copyString (buff.c_str(), strDesc2.at(0), param.key);
            copyString (buff.c_str(), strDesc2.at(1), param.value);
            reqUrl.param.push_back(param);
        }            
    }
    //LOG_WRITE(_LOG_DEBUG, "KEY = %s", m_key);
    return 0;
}
 


int32_t parseHttpMessage(char* buffer, int32_t len, HttpReq_t& httpReq) {
    int32_t i = 0;
    uint32_t lineStart = 0;
    uint32_t lineEnd = 0;
    uint32_t lineNo = 0;
    uint32_t dataSize = 0;
    std::string line;

    httpReq.sessionId = 0;
	httpReq.dataSize = 0;
	httpReq.keepAlive = false;
	httpReq.fragNumber = 0;

    while (i < len) {
        if (buffer[i] == '\r' && buffer[i + 1] == '\n') {
            lineEnd = i;
            if ((lineEnd -  lineStart)> 0) {
                line.assign(buffer + lineStart, lineEnd -  lineStart);
                if (lineNo == 0) {
                    StringDescList_t descList;
                    splitString(buffer, lineStart, lineEnd, " ", descList);
                    if (descList.size() > 0)
                        copyString(buffer, descList[0], httpReq.type);
                    if (descList.size() > 1) {
						std::string temp;
						copyString(buffer, descList[1], temp);
						parseUrl(temp, httpReq.url);
					}
                    if (descList.size() > 2)
                        copyString(buffer, descList[2], httpReq.version);
                    
                } else {
                    std::size_t pos = line.find(" ");
                    if (pos != std::string::npos && line.substr(0, pos) == "Content-Length:") {
                        httpReq.dataSize = atoi(line.substr(pos + 1).c_str());
                    } else if (pos != std::string::npos && line.substr(0, pos) == "Pragma:") {
						httpReq.fragNumber = atoi(line.substr(pos + 1).c_str());
					} else if (pos != std::string::npos && line.substr(0, pos) == "Origin:") {
						httpReq.origin = line.substr(pos + 1);
					} else if (pos != std::string::npos && line.substr(0, pos) == "Connection:") {
						httpReq.keepAlive = line.substr(pos + 1) == "keep-alive";
					}
                }
                lineNo++;
            } else {
                //End of requent header
                i += 2;
                break;
            } 
            lineStart = i + 2;
            i += 2;
        } else {
            i++;
        }
    }
    return 0;
}


void sendReplyError(BIO* sbio, std::string errorTxt, HttpReq_t& httpReq, char* outBuffer) {
	static char errorTemplate[] = "<!DOCTYPE html>\n \
                 <html>\n \
                 <body>\n \
                 <h1>%s</h1>\n \
                 </body>\n \
                 </html>";
	char errorPage[128];
	snprintf(errorPage, 127, errorTemplate, errorTxt.c_str());
	snprintf(outBuffer, HTTP_HEADER_SIZE_MAX -1 , "HTTP/1.1 200 OK\r\n" \
                             "Connection: close\r\n" \
							 "Server: %s\r\n" \
                             "Content-Type: text/html; charset=UTF-8\r\n" \
                             "Accept-Ranges: none\r\n" \
                             "Content-Encoding: UTF-8\r\n" \
                             "Cache-Control: no-store\r\n" \
                             "Content-Length: %d\r\n\r\n", HTTP_SERVER_NAME, strlen(errorPage));
	BIO_write(sbio, outBuffer, strlen(outBuffer));
    BIO_write(sbio, errorPage, strlen(errorPage));	
	httpReq.keepAlive = false;
}

void sendReplyPage(BIO* sbio, std::string fileName, const char* mime, HttpReq_t& httpReq, char* outBuffer) {
	std::ifstream fileIn(fileName);
    if(!fileIn.is_open()) {
		logWrite(HTTP_LOG_ERROR, "Unable to open file = %s, error = %d", fileName.c_str(), errno);  
		sendReplyError(sbio, "Resource not found!", httpReq, outBuffer);
		return;
    }
    std::ostringstream ss;
    ss << fileIn.rdbuf(); // reading data
    int32_t pageSize = ss.str().size() + 64;
	char* page = new char[pageSize];
	snprintf(page, pageSize - 1, ss.str().c_str(), g_serverIp.c_str(), g_httpsPort, g_videoFragSize);
	snprintf(outBuffer, HTTP_HEADER_SIZE_MAX -1 , "HTTP/1.1 200 OK\r\n" \
                             "Connection: %s\r\n" \
							 "Server: %s\r\n" \
                             "Content-Type: %s\r\n" \
							 "Accept-Ranges: none\r\n" \
                             "Cache-Control: no-store\r\n" \
                             "Content-Length: %d\r\n\r\n", 
							 httpReq.keepAlive ? "keep-alive" : "close",
							 HTTP_SERVER_NAME, mime, strlen(page));
	BIO_write(sbio, outBuffer, strlen(outBuffer));
    BIO_write(sbio, page, strlen(page));
	delete page;
	fileIn.close();
}


void sendReplyMediaEcho(BIO* sbio, uint8_t* data, HttpReq_t& httpReq, char* outBuffer) {
	snprintf(outBuffer, HTTP_HEADER_SIZE_MAX -1 , "HTTP/1.1 200 OK\r\n" \
                             "Connection: %s\r\n" \
							 "Server: %s\r\n" \
							 "Origin: %s\r\n" \
                             "Content-Type: application/octet-stream\r\n" \
                             "Accept-Ranges: none\r\n" \
							 "Pragma: %u\r\n" \
                             "Cache-Control: no-store\r\n" \
                             "Content-Length: %d\r\n\r\n",
                             httpReq.keepAlive ? "keep-alive" : "close",							 
							 HTTP_SERVER_NAME, httpReq.origin.c_str(), httpReq.fragNumber, httpReq.dataSize);
	BIO_write(sbio, outBuffer, strlen(outBuffer));
    BIO_write(sbio, data, httpReq.dataSize);
}


void connectionBody(void* param) {
	assert(param != nullptr);
	HttpConnction_t* connection = (HttpConnction_t*)param;
	logWrite(HTTP_LOG_DEBUG, "Staring http connection(%u)", connection->id);
	BIO* sbio = connection->sbio;
    //SSL Handshake fail for self signed certificate, nothing to worry.
    if (BIO_do_handshake(sbio) <= 0) {
		logWrite(HTTP_LOG_ERROR, "BIO_do_handshake failed");
		ERR_print_errors_fp(stderr);
		//exit(1);
	}
	char* line = new char[HTTP_LINE_SIZE_MAX];
	char* httpHeader = new char[HTTP_HEADER_SIZE_MAX];
	
	//We dont pre-allocate any memory to read the HTTP payload
	//(i.e. recorded media fragment in this case), will allocate 
	//based on needs.
	uint8_t* contentData = nullptr;
	uint32_t contentDataMaxSize = 0;
	HttpReq_t httpReq;
	do {
		int pos = 0;
		bool keepReading = true;
		//Read the entire HTTP header.
		do {
			int32_t len = BIO_gets(sbio, line, HTTP_LINE_SIZE_MAX);
			if (len <= 0) {
				if (BIO_should_read(sbio)) {
					logWrite(HTTP_LOG_DEBUG, "Need to read more");
				} else {
					keepReading = false;
				}
			} else {
				strncpy (httpHeader + pos, line, HTTP_HEADER_SIZE_MAX - pos);
				pos += len;
				if (line[0] = '\r' && line[1] == '\n') {
					logWrite(HTTP_LOG_DEBUG, "HTTP Message:\n%s", httpHeader);
					keepReading = false;
				}
			}
		} while (keepReading);
		if (pos <= 0) {
			BIO_flush(sbio);
			break;
		}

		parseHttpMessage (httpHeader, pos, httpReq);
		if (httpReq.dataSize > 0) {
			//Check if we need to grow the content buffer
			if (httpReq.dataSize > contentDataMaxSize) {
				if (contentData != nullptr)
					delete contentData;
				contentDataMaxSize = HTTP_MEDIA_PAYLOAD_MAX(httpReq.dataSize);
				contentData = new uint8_t[contentDataMaxSize];
				logWrite(HTTP_LOG_DEBUG, "Buffer allocated for media payload = %d", contentDataMaxSize);
			}
			int32_t len = BIO_read(sbio, contentData, httpReq.dataSize);
			if (len <= 0) {
				logWrite(HTTP_LOG_ERROR, "Unable to read content data");
			} else {
				logWrite(HTTP_LOG_DEBUG, "Read content data = %d", httpReq.dataSize);
			}
		}
		if(httpReq.url.cmd.empty() ||
		   httpReq.url.cmd == "" ||
		   httpReq.url.cmd == HTTP_CMD_LANDING_PAGE) {
			logWrite(HTTP_LOG_DEBUG, "Request: Landing page");
			sendReplyPage(sbio, HTTP_CMD_LANDING_PAGE, "text/html; charset=UTF-8", httpReq, httpHeader);
		} else if (httpReq.url.cmd == HTTP_CMD_MEDIA_ECHO) {
			logWrite(HTTP_LOG_DEBUG, "Request: Echo");
			sendReplyMediaEcho(sbio, contentData, httpReq, httpHeader);	
		} else if (httpReq.url.cmd == HTTP_CMD_FAVICON) {
			logWrite(HTTP_LOG_DEBUG, "Request: favicon.ico");
			sendReplyPage(sbio, HTTP_CMD_FAVICON, "image/x-icon", httpReq, httpHeader);
		} else {
			sendReplyError(sbio, "Invalid Request!", httpReq, httpHeader);
		}
		BIO_flush(sbio);
	} while (httpReq.keepAlive);
	
	BIO_free_all(sbio);	
	if (line != nullptr)
		delete line;
	if (httpHeader != nullptr)
		delete httpHeader;
	if (contentData != nullptr)
		delete contentData;
	logWrite(HTTP_LOG_DEBUG, "Ending http connection(%u)", connection->id);
	connection->state = CONNECTION_STATE_END;
}


void printUsage() {
    printf("Usage: server.exe -i ip -p port [-f frag] [-c certificate]\n");
    printf("\t -i ip\t\tWebserver IP\n");
    printf("\t -p port\tWebserver port\n");
	printf("\t -f frag\tVideo fragment size in second\n");
	printf("\t -c certificate\tFull path of the certificate(.pem file) for SSL\n");
}

int main(int argc, char** argv)
{
    if (argc < 3) {
		printUsage();
        exit(1);
    }
	
	int32_t opt = -1;
    while ((opt = getopt(argc, &argv[0], "i:p:f:c:")) != -1) {
        switch (opt) {
        case 'i': g_serverIp = optarg; break;
        case 'p': g_httpsPort = (uint16_t)atoi(optarg); break;
		case 'f': g_videoFragSize = (int32_t)atoi(optarg); break;
		case 'c': g_certificate = optarg; break;
        default:
            printUsage();
            exit(1);
        }
    }
	
	char* env = std::getenv("ENABLE_LOG");
	if (env != nullptr && atoi(env) == 1)
	    g_log = true;
	
	logWrite(HTTP_LOG_DEBUG, "Staring webserver @%s:%d", g_serverIp.c_str(), g_httpsPort);

    BIO *sbio, *bbio, *acpt;
	SSL *ssl;
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
		
	uint32_t connectionId = 0;
	HttpConnctionList_t connectionList;
	
    //Webserver main loop
    while (1) {	
        /* Setup server side SSL bio */
        sbio = BIO_new_ssl(ctx, 0);
        BIO_get_ssl(sbio, &ssl);
	
        bbio = BIO_new(BIO_f_buffer());
        sbio = BIO_push(bbio, sbio);
	    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

        char portStr[8];
	    snprintf(portStr, 8, "%d", g_httpsPort);
        if ((acpt = BIO_new_accept(portStr)) == nullptr) {
			logWrite(HTTP_LOG_ERROR, "BIO_new_accept failed");
		    exit(1);
	    }

        BIO_set_accept_bios(acpt, sbio);
	
	    if (BIO_do_accept(acpt) <= 0) {
			logWrite(HTTP_LOG_ERROR, "Error setting up accept BIO");
		    exit(1);
	    }
	
	    if (BIO_do_accept(acpt) <= 0) {
			logWrite(HTTP_LOG_ERROR, "Error in connection");
		    exit(1);
	    }
	
		logWrite(HTTP_LOG_DEBUG, "Received SSL connection");
		
        sbio = BIO_pop(acpt);
        BIO_free_all(acpt);
		
		//Create dedicated thread to handle new http connection.
		HttpConnction_t* connection = new HttpConnction_t();
		connection->id     = connectionId++;
		connection->sbio   = sbio;
		connection->state  = CONNECTION_STATE_INIT;
		connection->thread = new std::thread(connectionBody, connection);
		connectionList.push_back(connection);	

		//Cleanup dead connection.
		HttpConnctionList_t::iterator it = connectionList.begin();
		while (it != connectionList.end()) {
			HttpConnction_t* con = *it;
			if (con->state == CONNECTION_STATE_END) {
				con->thread->join();
				delete con->thread;
				delete con;
				it = connectionList.erase(it);
			} else {
				it++;
			}
		}
    }	 
	
    return 0;
}

