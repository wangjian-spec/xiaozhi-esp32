#include "eteacher_state_machine.h"

#include <algorithm>
#include <esp_log.h>

static const char* TAG = "EteacherStateMachine";

// State name strings for logging
static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "wifi_configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

EteacherStateMachine::EteacherStateMachine() {
}

const char* EteacherStateMachine::GetStateName(EteacherState state) {
    if (state >= 0 && state <= kEteacherStateFatalError) {
        return STATE_STRINGS[state];
    }
    return STATE_STRINGS[kEteacherStateFatalError + 1];
}

bool EteacherStateMachine::IsValidTransition(EteacherState from, EteacherState to) const {
    // Allow transition to the same state (no-op)
    if (from == to) {
        return true;
    }

    // Define valid state transitions based on the state diagram
    switch (from) {
        case kEteacherStateUnknown:
            // Can only go to starting
            return to == kEteacherStateStarting;

        case kEteacherStateStarting:
            // Can go to wifi configuring or activating
            return to == kEteacherStateWifiConfiguring ||
                   to == kEteacherStateActivating;

        case kEteacherStateWifiConfiguring:
            // Can go to activating (after wifi connected) or audio testing
            return to == kEteacherStateActivating ||
                   to == kEteacherStateAudioTesting;

        case kEteacherStateAudioTesting:
            // Can go back to wifi configuring
            return to == kEteacherStateWifiConfiguring;
        case kEteacherStateActivating:
            // Can go to upgrading, idle, or back to wifi configuring (on error)
            return to == kEteacherStateUpgrading ||
                   to == kEteacherStateIdle ||
                   to == kEteacherStateWifiConfiguring;

        case kEteacherStateUpgrading:
            // Can go to idle (upgrade failed) or activating
            return to == kEteacherStateIdle ||
                   to == kEteacherStateActivating;

        case kEteacherStateIdle:
            // Can go to connecting, listening (manual mode), speaking, activating, upgrading, or wifi configuring
            return to == kEteacherStateConnecting ||
                   to == kEteacherStateListening ||
                   to == kEteacherStateSpeaking ||
                   to == kEteacherStateActivating ||
                   to == kEteacherStateUpgrading ||
                   to == kEteacherStateWifiConfiguring;
        case kEteacherStateConnecting:
            // Can go to idle (failed) or listening (success)
            return to == kEteacherStateIdle ||
                   to == kEteacherStateListening;
        case kEteacherStateListening:
            // Can go to speaking or idle
            return to == kEteacherStateSpeaking ||
                   to == kEteacherStateIdle;
        case kEteacherStateSpeaking:
            // Can go to listening or idle
            return to == kEteacherStateListening ||
                   to == kEteacherStateIdle;
        case kEteacherStateFatalError:
            // Cannot transition out of fatal error
            return false;

        default:
            return false;
    }
}


bool EteacherStateMachine::CanTransitionTo(EteacherState target) const {
    return IsValidTransition(current_state_.load(), target);
}

bool EteacherStateMachine::TransitionTo(EteacherState new_state) {
    EteacherState old_state = current_state_.load();
    
    // No-op if already in the target state
    if (old_state == new_state) {
        return true;
    }

    // Validate transition
    if (!IsValidTransition(old_state, new_state)) {
        ESP_LOGW(TAG, "Invalid state transition: %s -> %s",
                 GetStateName(old_state), GetStateName(new_state));
        return false;
    }

    // Perform transition
    current_state_.store(new_state);
    ESP_LOGI(TAG, "State: %s -> %s",
             GetStateName(old_state), GetStateName(new_state));

    // Notify callback
    NotifyStateChange(old_state, new_state);
    return true;
}

int EteacherStateMachine::AddStateChangeListener(StateCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    int id = next_listener_id_++;
    listeners_.emplace_back(id, std::move(callback));
    return id;
}

void EteacherStateMachine::RemoveStateChangeListener(int listener_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
            [listener_id](const auto& p) { return p.first == listener_id; }),
        listeners_.end());
}

void EteacherStateMachine::NotifyStateChange(EteacherState old_state, EteacherState new_state) {
    std::vector<StateCallback> callbacks_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_copy.reserve(listeners_.size());
        for (const auto& [id, cb] : listeners_) {
            callbacks_copy.push_back(cb);
        }
    }
    
    for (const auto& cb : callbacks_copy) {
        cb(old_state, new_state);
    }
}
