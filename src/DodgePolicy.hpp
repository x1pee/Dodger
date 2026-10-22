#pragma once
#include <cmath>

// Pure policy only. The caller obtains authoritative values on the game thread.
namespace DodgePolicy {
struct Availability { bool available; const char* reason; };
inline Availability fullDashAvailable(bool gameCanDash, bool strafing,
                                      float stamina, float adjustedCost,
                                      int tinyDashLevel) {
    if (!gameCanDash) return {false, "game_CanDash_rejected"};
    if (strafing) return {false, "strafe_would_make_short_dash"};
    if (!std::isfinite(stamina) || !std::isfinite(adjustedCost) ||
        adjustedCost < 0.f) return {false, "dash_stamina_unknown"};
    if (stamina < adjustedCost) return {false, "insufficient_stamina_for_iframe_dash"};
    if (tinyDashLevel < 0) return {false, "dash_injury_state_unknown"};
    if (tinyDashLevel == 3) return {false, "TinyDash_3_has_no_dodge_iframes"};
    return {true, "full_dash_can_start_not_yet_iframes"};
}

// Use two consecutive samples of the same dash sequence and animator state.
// clockSeconds is scaled game time for Normal/Fixed, unscaled for UnscaledTime.
// speedStable must include unchanged update mode, Animator/state speed and timeScale
// where applicable. A state transition, a stale sample or a new dash invalidates it.
inline float observedNormalizedRate(float previous, float current,
                                    double clockSeconds,
                                    bool sameDashState, bool speedStable) {
    if (!sameDashState || !speedStable || !std::isfinite(previous) ||
        !std::isfinite(current) || !std::isfinite(clockSeconds) ||
        previous < 0.f || current < previous || clockSeconds <= 0.000001 ||
        clockSeconds > 0.25) return 0.f;
    float rate = static_cast<float>((current - previous) / clockSeconds);
    return std::isfinite(rate) && rate > 0.f ? rate : 0.f;
}

struct Coverage { bool known; bool covers; float remainingMilliseconds; const char* reason; };
// clockScale = Time.timeScale for Normal/Fixed; 1 for UnscaledTime.
// ETA and safety margin are wall-clock milliseconds. No fixed dash immunity assumed.
inline Coverage currentIFramesCover(bool actualIFrames, float normalizedTime,
                                    float observedRate, float clockScale,
                                    float etaMilliseconds, float safetyMilliseconds) {
    if (!actualIFrames) return {true, false, 0.f, "current_dash_has_no_iframes"};
    if (!std::isfinite(normalizedTime) || normalizedTime < 0.f || normalizedTime >= 0.4f ||
        !std::isfinite(observedRate) || observedRate <= 0.f ||
        !std::isfinite(clockScale) || clockScale <= 0.f ||
        !std::isfinite(etaMilliseconds) || etaMilliseconds < 0.f ||
        !std::isfinite(safetyMilliseconds) || safetyMilliseconds < 0.f)
        return {false, false, 0.f, "current_iframe_projection_unknown"};
    const float remaining = (0.4f - normalizedTime) / observedRate / clockScale * 1000.f;
    if (!std::isfinite(remaining)) return {false, false, 0.f, "current_iframe_projection_unknown"};
    bool covers = etaMilliseconds + safetyMilliseconds < remaining;
    return {true, covers, remaining,
        covers ? "current_dodge_iframes_cover_impact" : "current_dodge_iframes_end_before_impact"};
}
}
