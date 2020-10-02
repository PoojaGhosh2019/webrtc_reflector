#include "http.h"

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
 


bool parseHttpMessage(char* buffer, int32_t len, HttpReq_t& httpReq) {
    int32_t i = 0;
    uint32_t lineStart = 0;
    uint32_t lineEnd = 0;
    uint32_t lineNo = 0;
    uint32_t dataSize = 0;
    std::string line;

    httpReq.sessionId = 0;
	httpReq.data = nullptr;
	httpReq.dataSize = 0;
	httpReq.keepAlive = false;
	httpReq.pragma = 0;
	bool ret = false;
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
						httpReq.pragma = atoi(line.substr(pos + 1).c_str());
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
				ret = true;
				if (i < len)
					httpReq.data = (uint8_t*)&buffer[i];
                break;
            } 
            lineStart = i + 2;
            i += 2;
        } else {
            i++;
        }
    }
    return ret;
}
