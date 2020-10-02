#include "message_queue.h"

using namespace std::chrono_literals;

MessageQueue::MessageQueue():
    m_queue(),
	m_mutex(),
	m_condvar() 
{
}

MessageQueue::~MessageQueue() {
}

int32_t MessageQueue::enqueue(std::string& message) {
	std::unique_lock<std::mutex> lock(m_mutex);
	m_queue.push(message);
	m_condvar.notify_all();
	return 0;
}

int32_t MessageQueue::dequeue(std::string& message) {
    std::unique_lock<std::mutex> lock(m_mutex);
	while (m_queue.size() == 0) {
		if (m_condvar.wait_for(lock, 100ms) == std::cv_status::timeout)
			return -1;
	}
	message = m_queue.front();
    m_queue.pop();
	return 0;
}