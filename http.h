#ifndef HTTP_H
#define HTTP_H

#include <string>
#include <vector>

#define HTTP_LINE_SIZE_MAX    (512)
#define HTTP_HEADER_SIZE_MAX  (1024)

#define HTTP_SERVER_NAME      "WebRTC Reflector"

#define HTTP_MEDIA_PAYLOAD_MAX(x) (((x + 102399) / 102400) * 102400)

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
	uint8_t*     data;
	uint32_t     dataSize;
	uint32_t     pragma;
} HttpReq_t;


typedef struct {
    uint32_t start;
    uint32_t length;
} StringDesc_t;

typedef std::vector<StringDesc_t> StringDescList_t;

bool parseHttpMessage(char* buffer, int32_t len, HttpReq_t& httpReq);


#endif //HTTP_H


