#include "BuildProfile.hpp"

#include <array>

namespace preyht {

// One file per store, every build for that store inside it.
extern const BuildProfile kSteamProfile_20190703;
extern const BuildProfile kGdkProfile_20230519;

namespace {

// Newest build first. The top entry is the diagnostic primary: when nothing
// matches at all, it is what the running EXE is compared against to say which
// side of it the player is on.
constexpr std::array<const BuildProfile*, 2> kKnownProfiles{{
    &kGdkProfile_20230519,
    &kSteamProfile_20190703,
}};

/// Builds this mod will not touch on purpose, and why.
struct RefusedBuild {
    cameraunlock::memory::PeFingerprint fp;
    const char*                         reason;
};

// Prey: Mooncrash / Typhon Hunter ships as its own PreyDll.dll under
// Whiplash\Binaries, once per store. Typhon Hunter is Prey's multiplayer mode,
// where a mod that changes one player's view of a shared world does not belong.
// Do not turn either of these into a build profile: that would need
// gEnv->bMultiplayer re-derived for that binary and the multiplayer check proven
// in an actual match first, and neither has been done.
constexpr const char* kMooncrashReason =
    "this is the Prey: Mooncrash / Typhon Hunter build. Typhon Hunter is "
    "multiplayer, so head tracking deliberately does not run on it";

constexpr std::array<RefusedBuild, 2> kRefusedBuilds{{
    { { 0x5D2352B3u, 0x02FB9000u, 0x00000000u }, kMooncrashReason },  // Steam
    { { 0x6467AF55u, 0x02DD0000u, 0x00000000u }, kMooncrashReason },  // GDK
}};

}  // namespace

ProfileMatch MatchBuildProfile(const cameraunlock::memory::PeFingerprint& running) {
    const BuildProfile& primary = *kKnownProfiles.front();
    ProfileMatch out;
    out.newest_known_name = primary.name;

    for (const auto& refused : kRefusedBuilds) {
        if (running.Matches(refused.fp)) {
            out.reason  = refused.reason;
            out.refused = true;
            return out;
        }
    }

    for (const BuildProfile* p : kKnownProfiles) {
        if (!running.Matches(p->fp)) continue;
        if (p->IsComplete()) {
            out.profile = p;
            return out;
        }
        out.reason = "this build has a placeholder profile whose addresses have not been "
                     "derived yet";
        return out;
    }

    out.reason =
        running.TimeDateStamp > primary.fp.TimeDateStamp
            ? "this build is NEWER than any this mod knows about - check the releases page "
              "for an updated mod"
        : running.TimeDateStamp < primary.fp.TimeDateStamp
            ? "this build is OLDER than any this mod knows about - let the store finish "
              "updating"
            : "same build date but a different size or checksum - a repacked or modified "
              "PreyDll.dll, which this mod will not engage on";
    return out;
}

}  // namespace preyht
