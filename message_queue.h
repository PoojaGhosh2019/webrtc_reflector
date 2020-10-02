#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <queue>
#include <mutex>
#include <condition_variable>

class MessageQueue {
public:
	MessageQueue();
	~MessageQueue();
	int32_t enqueue(std::string& message);
	int32_t dequeue(std::string& message);
private:
	std::queue<std::string> m_queue;
    std::mutex              m_mutex;
    std::condition_variable m_condvar;
}; 

#endif //MESSAGE_QUEUE_H