#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>


#include "observer.h"
#include "json/json.h"

#include "base/checks.h"
#include "base/logging.h"
#include "api/test/fakeconstraints.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/peerconnectioninterface.h"
#include "pc/videotracksource.h"

#include "log.h"

const char kStreamLabel[] = "stream_label";
const char kVideoLabel[] = "video_label";

// Names used for a IceCandidate JSON object.
const char kCandidateSdpMidName[] = "sdpMid";
const char kCandidateSdpMlineIndexName[] = "sdpMLineIndex";
const char kCandidateSdpName[] = "candidate";

// Names used for a SessionDescription JSON object.
const char kSessionDescriptionTypeName[] = "type";
const char kSessionDescriptionSdpName[] = "sdp";

using namespace std;


string convertSignalingStateToString(webrtc::PeerConnectionInterface::SignalingState new_state) {
    string state;
    switch (new_state) {
        case webrtc::PeerConnectionInterface::kStable: state = "Stable"; break;
        case webrtc::PeerConnectionInterface::kHaveLocalOffer: state = "HaveLocalOffer"; break;
        case webrtc::PeerConnectionInterface::kHaveLocalPrAnswer: state = "HaveLocalPrAnswer"; break;
        case webrtc::PeerConnectionInterface::kHaveRemoteOffer: state = "HaveRemoteOffer"; break;
        case webrtc::PeerConnectionInterface::kHaveRemotePrAnswer: state = "HaveRemotePrAnswer"; break;
        case webrtc::PeerConnectionInterface::kClosed: state = "Closed"; break;
        default: state = "Unknown"; break;
    }
    return state;
}

string convertIceGatheringStateToString(webrtc::PeerConnectionInterface::IceGatheringState new_state) {
    string state;
    switch (new_state) {
        case webrtc::PeerConnectionInterface::kIceGatheringNew: state = "IceGatheringNew"; break;
        case webrtc::PeerConnectionInterface::kIceGatheringGathering: state = "IceGatheringGathering"; break;
        case webrtc::PeerConnectionInterface::kIceGatheringComplete: state = "IceGatheringComplete"; break;
        default: state = "Unknown"; break;
    }
    return state;
}

string convertIceConnectionStateToString(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
    string state;
    switch (new_state) {
        case webrtc::PeerConnectionInterface::kIceConnectionNew: state = "IceConnectionNew"; break;
        case webrtc::PeerConnectionInterface::kIceConnectionChecking: state = "IceConnectionChecking"; break;
        case webrtc::PeerConnectionInterface::kIceConnectionConnected: state = "IceConnectionConnected"; break;
        case webrtc::PeerConnectionInterface::kIceConnectionCompleted: state = "IceConnectionCompleted"; break;
        case webrtc::PeerConnectionInterface::kIceConnectionFailed: state = "IceConnectionFailed"; break;
        case webrtc::PeerConnectionInterface::kIceConnectionDisconnected: state = "IceConnectionDisconnected"; break;
        case webrtc::PeerConnectionInterface::kIceConnectionClosed: state = "IceConnectionClosed"; break;
        default: state = "Unknown"; break;
    }
    return state;
}


class DummySetSessionDescriptionObserver: public webrtc::SetSessionDescriptionObserver {
 public:
  static DummySetSessionDescriptionObserver* Create() {
    return new rtc::RefCountedObject<DummySetSessionDescriptionObserver>();
  }
  virtual void OnSuccess() { 
	logWrite(HTTP_LOG_DEBUG, "SessionDescriptionObserver::OnSuccess");
  }
  virtual void OnFailure(const std::string& error) {
	logWrite(HTTP_LOG_DEBUG, "SessionDescriptionObserver::OnFailure");
  }
};


Observer::Observer(uint32_t peerId): 
    m_peerId(peerId),
	m_peer_connection_(),
	m_peer_connection_factory_(),
	m_iceCandidateMsg(),
	m_remoteIceCandidateMsg(),
	m_msgQueue(),
	m_mutex(),
	m_localStream(),
	m_localIceState(webrtc::PeerConnectionInterface::kIceConnectionNew)
{    
}

Observer::~Observer() {
}

bool Observer::initPeerConnection() {
    //m_peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
    //    webrtc::CreateBuiltinAudioEncoderFactory(),
    //    webrtc::CreateBuiltinAudioDecoderFactory());
	
	m_peer_connection_factory_ = webrtc::CreatePeerConnectionFactory();
				
    if (!m_peer_connection_factory_.get()) {
		logWrite(HTTP_LOG_DEBUG, "Unable to create peer connection factory");
        return false;
    }

    webrtc::PeerConnectionInterface::RTCConfiguration config;
    webrtc::PeerConnectionInterface::IceServer server;

    //server.uri = "stun:stun.l.google.com:19302";
    //config.servers.push_back(server);
	
    webrtc::FakeConstraints constraints;
    constraints.AddOptional(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp, "true");

    m_peer_connection_ = m_peer_connection_factory_->CreatePeerConnection(config, &constraints, nullptr, nullptr, this);
    if (m_peer_connection_.get() == nullptr) {
		logWrite(HTTP_LOG_ERROR, "Unable to create peer connection");
        return false;
    } else {
		logWrite(HTTP_LOG_DEBUG, "Peer connection created");
    }

    return true;
}

bool Observer::deletePeerConnection() {
	if (m_peer_connection_.get()) {
		m_peer_connection_->Close();
        m_peer_connection_ = nullptr;
	}
    //m_peer_connection_factory_ = nullptr;
    m_peerId = -1;
	return true;
}


void Observer::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) {
	logWrite(HTTP_LOG_DEBUG, "Signaling state: %s", convertSignalingStateToString(new_state).c_str());
}

void Observer::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
	logWrite(HTTP_LOG_DEBUG, "Data Channel created from client");
}

void Observer::OnRenegotiationNeeded() {
	logWrite(HTTP_LOG_DEBUG, "OnRenegotiationNeeded");
}

void Observer::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
	logWrite(HTTP_LOG_DEBUG, "Ice connection state: %s", convertIceConnectionStateToString(new_state).c_str());
	std::unique_lock<std::mutex> lock(m_mutex);
	m_localIceState = new_state;
}

void Observer::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) {
	logWrite(HTTP_LOG_DEBUG, "Ice gathering state: %s", convertIceGatheringStateToString(new_state).c_str());
	if (new_state == webrtc::PeerConnectionInterface::kIceGatheringComplete) {
		sendSignalingMessage(m_iceCandidateMsg);
	}
}

void Observer::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
	logWrite(HTTP_LOG_DEBUG, "New ICE candidate created");

    Json::Value value;
    Json::StreamWriterBuilder builder;
    value[kCandidateSdpMidName] = candidate->sdp_mid();
    value[kCandidateSdpMlineIndexName] = candidate->sdp_mline_index();
    std::string sdp;
    if (!candidate->ToString(&sdp)) {
		logWrite(HTTP_LOG_ERROR, "Failed to serialize ICE candidate");
        return;
    }
    value[kCandidateSdpName] = sdp;    
    string json_str = Json::writeString(builder, value);

	if (m_iceCandidateMsg.empty()) {
		m_iceCandidateMsg = "ICE->";
		m_iceCandidateMsg += json_str;
	} else {
		m_iceCandidateMsg += "######";
		m_iceCandidateMsg += json_str;
	}
}


void Observer::addIceCandidate(std::string& iceStr) {
	if (m_peer_connection_ == nullptr) {
		logWrite(HTTP_LOG_ERROR, "m_peer_connection_ not created yet");
		return;
	}
	
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    Json::Value jmessage;
    string parse_error;
    if (!reader->parse(iceStr.c_str(), iceStr.c_str() + iceStr.size(), &jmessage, &parse_error)) {
		logWrite(HTTP_LOG_ERROR, "Unable to parse ICE candidate: %s", iceStr.c_str());
        return;
    }
	
    string sdp_mid = jmessage[kCandidateSdpMidName].asString();
    int sdp_mlineindex = jmessage[kCandidateSdpMlineIndexName].asInt();
    string sdp = jmessage[kCandidateSdpName].asString();
        
    webrtc::SdpParseError ice_error;
    std::unique_ptr<webrtc::IceCandidateInterface> candidate(webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &ice_error));
    if (!candidate.get()) {
		logWrite(HTTP_LOG_ERROR, "Invalid ICE candidate: %s", iceStr.c_str());
        return;
    }
    if (!m_peer_connection_->AddIceCandidate(candidate.get())) {
		logWrite(HTTP_LOG_ERROR, "Unable to add ICE candidate: %s", iceStr.c_str());
        return;
    }
}

void Observer::onSignalingMessage(int id, const std::string& message) {
	
	std::size_t pos = message.find("->", 0);
	std::string cmd = message.substr(0, pos);
	std::string payload = message.substr(pos + strlen("->"));
	
	if (cmd == "SDP") {
        //Either SDP offer or answer
		Json::CharReaderBuilder builder;
		Json::CharReader* reader = builder.newCharReader();
		Json::Value jmessage;
		string parse_error;
		if (!reader->parse(payload.c_str(), payload.c_str() + payload.size(), &jmessage, &parse_error)) {
			logWrite(HTTP_LOG_ERROR, "Unable to parse SDP: %s", payload.c_str());
			return;
		}
	    string type = jmessage["type"].asString();
        string sdp = jmessage["sdp"].asString();
		logWrite(HTTP_LOG_DEBUG, "SDP");
		logWrite(HTTP_LOG_DEBUG, "%s", sdp.c_str());
        
		webrtc::SdpParseError sdp_error;
        webrtc::SessionDescriptionInterface* session_description(webrtc::CreateSessionDescription(type, sdp, &sdp_error));
        if (!session_description) {
			logWrite(HTTP_LOG_ERROR, "Invalid SDP: %s", payload.c_str());
            return;
        }

        if (!initPeerConnection()) {
			return;
		}	
		
        m_peer_connection_.get()->SetRemoteDescription(DummySetSessionDescriptionObserver::Create(), session_description);		
		
        if (session_description->type() == webrtc::SessionDescriptionInterface::kOffer) {
			logWrite(HTTP_LOG_DEBUG, "Received SDP Offer from peer: %u", m_peerId);
			webrtc::PeerConnectionInterface::RTCOfferAnswerOptions option(1, 1, false, false, false);
            m_peer_connection_.get()->CreateAnswer(this, option);
		} else {
			logWrite(HTTP_LOG_DEBUG, "Received SDP Answer from peer: %u", m_peerId);
		}
    } else if (cmd == "ICE") {
		if (m_peer_connection_ == nullptr) {
			logWrite(HTTP_LOG_ERROR, "Unable to add ICE candidate since m_peer_connection_ is NULL");
			m_remoteIceCandidateMsg = payload;
			return;
		}
		
		std::string iceStr;
		std::size_t startPos = 0;
		while (1) {
			std::size_t pos = payload.find("######", startPos);
			if (pos != std::string::npos) {
				iceStr = payload.substr(startPos, pos - startPos);
				startPos += pos - startPos + 6;
				addIceCandidate(iceStr);
			} else {
				iceStr = payload.substr(startPos, pos - payload.size());
				addIceCandidate(iceStr);
				break;
			}
		}		
        logWrite(HTTP_LOG_DEBUG, "Received ICE Candidate from peer: %u", m_peerId);
    } else {
		logWrite(HTTP_LOG_DEBUG, "Unknown message: %s", message.c_str());
	}
}


void Observer::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
	assert(m_peer_connection_ != nullptr);
		
    string sdp;
    desc->ToString(&sdp);

    Json::Value value;
    Json::StreamWriterBuilder builder;
    value[kSessionDescriptionTypeName] = desc->type();
    value[kSessionDescriptionSdpName] = sdp;    
    string json_str = Json::writeString(builder, value);
	logWrite(HTTP_LOG_DEBUG, "Sending SDP Answer to peer: %u", m_peerId);
    logWrite(HTTP_LOG_DEBUG, "%s", json_str.c_str());

	std::string sdpAnswer = "SDP->";
	sdpAnswer += json_str;
	
    sendSignalingMessage(sdpAnswer);
			
	m_peer_connection_.get()->SetLocalDescription(DummySetSessionDescriptionObserver::Create(), desc);
	if (!m_remoteIceCandidateMsg.empty()) {
		std::string iceStr;
		std::size_t startPos = 0;
		while (1) {
			std::size_t pos = m_remoteIceCandidateMsg.find("######", startPos);
			if (pos != std::string::npos) {
				iceStr = m_remoteIceCandidateMsg.substr(startPos, pos - startPos);
				startPos += pos - startPos + 6;
				addIceCandidate(iceStr);
			} else {
				iceStr = m_remoteIceCandidateMsg.substr(startPos, pos - m_remoteIceCandidateMsg.size());
				addIceCandidate(iceStr);
				break;
			}
		}
	} 
}
    
void Observer::OnFailure(const std::string& error) {
	logWrite(HTTP_LOG_DEBUG, "Unable to create SDP, error: %s", error.c_str());
}


void Observer::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
	webrtc::VideoTrackVector videoTracks = stream->GetVideoTracks();
	webrtc::AudioTrackVector audioTracks = stream->GetAudioTracks();

	m_localStream = m_peer_connection_factory_->CreateLocalMediaStream(kStreamLabel);
	webrtc::AudioTrackVector audioTracksOld = m_localStream->GetAudioTracks();
	logWrite(HTTP_LOG_DEBUG, "Old audio tracks: %d", audioTracksOld.size());
	
	if (videoTracks.size() > 0) {
		logWrite(HTTP_LOG_DEBUG, "Video track found: %d", videoTracks.size());
		rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track = videoTracks.at(0);
        if (!m_localStream->AddTrack(video_track)) {
			logWrite(HTTP_LOG_ERROR, "Failed to add video track to stream");
        }
	} else {
		logWrite(HTTP_LOG_ERROR, "Video track not found");
	}
	if (audioTracks.size() > 0) {
		logWrite(HTTP_LOG_DEBUG, "Audio track found %d",audioTracks.size());
		rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track = audioTracks.at(0);
        if (!m_localStream->AddTrack(audio_track)) {
		   logWrite(HTTP_LOG_ERROR, "Failed to add audio track to stream");
        }
		//audio_track->set_enabled(false);
	} else {
		logWrite(HTTP_LOG_ERROR, "Audio track not found");
	}
	
	if (!m_peer_connection_->AddStream(m_localStream)) {
		logWrite(HTTP_LOG_ERROR, "Unable to add streams");
	} else {
		logWrite(HTTP_LOG_DEBUG, "Streams added");
	}
}


void Observer::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface>) {
	logWrite(HTTP_LOG_DEBUG, "OnRemoveStream");
}

int32_t Observer::sendSignalingMessage(std::string& message) {
	std::unique_lock<std::mutex> lock(m_mutex);
	m_msgQueue.push(message);
	return 0;
}

int32_t Observer::getSignalingMessageCount() {
	std::unique_lock<std::mutex> lock(m_mutex);
	return m_msgQueue.size();
}

int32_t Observer::getSignalingMessage(std::string& message) {
	std::unique_lock<std::mutex> lock(m_mutex);
	if (m_msgQueue.size() == 0)
		return -1;
	message = m_msgQueue.front();
    m_msgQueue.pop();
	return 0;
}


uint32_t Observer::getPeerId() {
    return m_peerId;
}

webrtc::PeerConnectionInterface::IceConnectionState Observer::getIceState() {
    std::unique_lock<std::mutex> lock(m_mutex);
	return m_localIceState;
}