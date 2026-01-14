#ifndef ETEACHER_STATE_MACHINE_H
#define ETEACHER_STATE_MACHINE_H

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include "eteacher_state.h"

/**
 * EteacherStateMachine - Manages device state transitions with validation
 * 
 * This class ensures strict state transition rules and provides a callback mechanism
 * for components to react to state changes.
 */
class EteacherStateMachine {
public:
    EteacherStateMachine();
    ~EteacherStateMachine() = default;

    // Delete copy constructor and assignment operator
    EteacherStateMachine(const EteacherStateMachine&) = delete;
    EteacherStateMachine& operator=(const EteacherStateMachine&) = delete;
    /**
     * Get the current device state
     */
    EteacherState GetState() const { return current_state_.load(); }
    /**
     * Attempt to transition to a new state
     * @param new_state The target state
     * @return true if transition was successful, false if invalid transition
     */
    bool TransitionTo(EteacherState new_state);

    /**
     * Check if transition to target state is valid from current state
     */
    bool CanTransitionTo(EteacherState target) const;

    /**
     * State change callback type
     * Parameters: old_state, new_state
     */
    using StateCallback = std::function<void(EteacherState, EteacherState)>;

    /**
     * Add a state change listener (observer pattern)
     * Callback is invoked in the context of the caller of TransitionTo()
     * @return listener id for removal
     */
    int AddStateChangeListener(StateCallback callback);

    /**
     * Remove a state change listener by id
     */
    void RemoveStateChangeListener(int listener_id);

    /**
     * Get state name string for logging
     */
    static const char* GetStateName(EteacherState state);

private:
    std::atomic<EteacherState> current_state_{kEteacherStateUnknown};
    std::vector<std::pair<int, StateCallback>> listeners_;
    int next_listener_id_{0};
    std::mutex mutex_;

    /**
     * Check if transition from source to target is valid
     */
    bool IsValidTransition(EteacherState from, EteacherState to) const;

    /**
     * Notify callback of state change
     */
    void NotifyStateChange(EteacherState old_state, EteacherState new_state);
};

#endif // ETEACHER_STATE_MACHINE_H