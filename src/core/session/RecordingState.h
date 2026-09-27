#pragma once

// Recording lifecycle state machine (pure logic, unit tested).
//
//   Idle/Error --start--> Starting --ok--> Recording <--pause/resume--> Paused
//   Starting --fail--> Error
//   Recording/Paused --stop--> Stopping --> Finalizing --> Idle (or Error)
//
// Every request is validated: a second Start while starting, Stop while already
// stopping, Pause while not recording etc. are rejected instead of racing.

namespace luma::session {

enum class RecState { Idle, Starting, Recording, Paused, Stopping, Finalizing, Error };

enum class RecEvent {
    StartRequested,
    StartSucceeded,
    StartFailed,
    PauseRequested,
    ResumeRequested,
    StopRequested,   // user, hotkey, disk-space guard or pipeline error
    SessionStopped,  // encoders flushed, file closed
    FinalizeDone,    // optional MP4 conversion + history done
    FinalizeFailed,
};

// Returns the next state, or `from` unchanged when the event is not allowed in
// that state (the caller should then ignore the request).
constexpr RecState nextState(RecState from, RecEvent e)
{
    switch (from) {
    case RecState::Idle:
    case RecState::Error:
        return e == RecEvent::StartRequested ? RecState::Starting : from;
    case RecState::Starting:
        if (e == RecEvent::StartSucceeded) return RecState::Recording;
        if (e == RecEvent::StartFailed) return RecState::Error;
        return from;
    case RecState::Recording:
        if (e == RecEvent::PauseRequested) return RecState::Paused;
        if (e == RecEvent::StopRequested) return RecState::Stopping;
        return from;
    case RecState::Paused:
        if (e == RecEvent::ResumeRequested) return RecState::Recording;
        if (e == RecEvent::StopRequested) return RecState::Stopping;
        return from;
    case RecState::Stopping:
        return e == RecEvent::SessionStopped ? RecState::Finalizing : from;
    case RecState::Finalizing:
        if (e == RecEvent::FinalizeDone) return RecState::Idle;
        if (e == RecEvent::FinalizeFailed) return RecState::Error;
        return from;
    }
    return from;
}

constexpr bool isBusy(RecState s)
{
    return s != RecState::Idle && s != RecState::Error;
}

constexpr bool isCapturing(RecState s)
{
    return s == RecState::Recording || s == RecState::Paused;
}

constexpr const char* stateName(RecState s)
{
    switch (s) {
    case RecState::Idle: return "Idle";
    case RecState::Starting: return "Starting";
    case RecState::Recording: return "Recording";
    case RecState::Paused: return "Paused";
    case RecState::Stopping: return "Stopping";
    case RecState::Finalizing: return "Finalizing";
    case RecState::Error: return "Error";
    }
    return "?";
}

} // namespace luma::session
