#ifndef _OBSERVER_H
#define _OBSERVER_H

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "api/mediastreaminterface.h"
#include "api/peerconnectioninterface.h"
#include "api/rtpsenderinterface.h"

using namespace std;

typedef vector<string> message_queue_t;


class Observer: public webrtc::PeerConnectionObserver,
                public webrtc::CreateSessionDescriptionObserver {
public:
    Observer(uint32_t peerId);
    virtual ~Observer();

    virtual void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state);
    virtual void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel);
    virtual void OnRenegotiationNeeded();
    virtual void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state);
    virtual void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state);
    virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate);
	virtual void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface>);
	virtual void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface>);

    virtual void onSignalingMessage(int id, const std::string& message);

    // CreateSessionDescriptionObserver implementation.
    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
    void OnFailure(const std::string& error) override;

	void addIceCandidate(std::string& iceStr);

    bool initPeerConnection();
	bool deletePeerConnection();
    
	int32_t getSignalingMessage(std::string& message);
	int32_t getSignalingMessageCount();
	int32_t sendSignalingMessage(std::string& message);
	
	uint32_t getPeerId();
    webrtc::PeerConnectionInterface::IceConnectionState getIceState();
	
private:
    int                                                        m_peerId;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface>        m_peer_connection_;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peer_connection_factory_;
	std::string                                                m_iceCandidateMsg;
	std::string                                                m_remoteIceCandidateMsg;
	std::queue<std::string>                                    m_msgQueue;
	std::mutex                                                 m_mutex;
	rtc::scoped_refptr<webrtc::MediaStreamInterface>           m_localStream;
	webrtc::PeerConnectionInterface::IceConnectionState        m_localIceState;
};

#endif //_OBSERVER_H