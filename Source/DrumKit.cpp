#include "DrumKit.h"

namespace aidrum
{
    namespace
    {
        // GM helpers for readability.
        constexpr int kBoomyKick   = 35; // Acoustic Bass Drum — round, boomy, resonant
        constexpr int kTightKick   = 36; // Bass Drum 1        — tight, clicky, modern
        constexpr int kWoodSnare   = 38; // Acoustic Snare     — fat, wooden, classic
        constexpr int kElecSnare   = 40; // Electric Snare     — gated, synthetic, tight
        constexpr int kSide        = 37; // Side Stick         — rim / cross-stick
        constexpr int kClap        = 39; // Hand Clap
        constexpr int kClosed      = 42;
        constexpr int kPedal       = 44;
        constexpr int kOpen        = 46;
        constexpr int kRide        = 51;
        constexpr int kBell        = 53;
        constexpr int kRide2       = 59;
        constexpr int kCrash       = 49;
        constexpr int kCrash2      = 57;
        constexpr int kChina       = 52;
        constexpr int kSplash      = 55;
        constexpr int kLowFloor    = 41;
        constexpr int kHighFloor   = 43;
        constexpr int kLowTom      = 45;
        constexpr int kMidTom      = 47;
        constexpr int kHiMidTom    = 48;
        constexpr int kHighTom     = 50;

        DrumKitProfile makeProfile()
        {
            return DrumKitProfile{};
        }
    }

    const DrumKitProfile& drumKitProfile (DrumKit kit)
    {
        // Build once, return const ref.
        static const auto kProfiles = [] () -> std::vector<DrumKitProfile>
        {
            std::vector<DrumKitProfile> v (static_cast<size_t> (DrumKit::Count));

            // -------- JAZZ ---------------------------------------------------
            // Ludwig Bebop Jazz: brush-style feel, sidestick ghosts, ride-bell
            // accents, low velocity, high ghost density, small boomy kick.
            {
                auto& p = v[(int) DrumKit::LudwigBebopJazz];
                p = makeProfile();
                p.kick         = kBoomyKick;   // small 18" boomy resonant kick
                p.snare        = kWoodSnare;
                p.ghostSnare   = kSide;        // brush cross-stick
                p.closedHat    = kClosed;
                p.openHat      = kOpen;
                p.ride         = kRide;
                p.rideBell     = kBell;
                p.crash        = kSplash;      // small splash instead of crash
                p.lowTom       = kLowTom;      // rack + small floor, no deep thud
                p.midTom       = kMidTom;
                p.highTom      = kHiMidTom;
                p.velocityScale  = 0.72f;
                p.ghostBoost     = 1.6f;
                p.accentBoost    = 0.90f;
                p.ghostThreshold = 0.60f;
            }
            // Gretsch Cool Jazz: even softer, more sidestick, ride focused.
            {
                auto& p = v[(int) DrumKit::GretschCoolJazz];
                p = makeProfile();
                p.kick         = kBoomyKick;
                p.snare        = kSide;        // cross-stick as the main snare voice
                p.ghostSnare   = kSide;
                p.closedHat    = kClosed;
                p.openHat      = kOpen;
                p.ride         = kRide;
                p.rideBell     = kBell;
                p.crash        = kSplash;
                p.velocityScale  = 0.65f;
                p.ghostBoost     = 1.7f;
                p.accentBoost    = 0.85f;
                p.ghostThreshold = 0.65f;
            }

            // -------- CLASSIC ROCK ------------------------------------------
            // Ludwig Supraphonic (Bonham/Mitchell-style): huge round kick, fat
            // wood snare, ride-heavy, loud accents, minimal ghosts.
            {
                auto& p = v[(int) DrumKit::LudwigSupraphonicClassicRock];
                p = makeProfile();
                p.kick       = kBoomyKick;
                p.snare      = kWoodSnare;
                p.ghostSnare = kWoodSnare;
                p.ride       = kRide;
                p.crash      = kCrash;
                p.crashAlt   = kCrash2;
                p.lowTom     = kLowFloor;
                p.midTom     = kLowTom;
                p.highTom    = kHighTom;
                p.velocityScale  = 1.00f;
                p.ghostBoost     = 0.6f;
                p.accentBoost    = 1.05f;
            }
            // Ludwig Vistalite '70s: big open toms, open hats, John-Bonham feel.
            {
                auto& p = v[(int) DrumKit::LudwigVistaliteSeventiesRock];
                p = makeProfile();
                p.kick       = kBoomyKick;
                p.snare      = kWoodSnare;
                p.ghostSnare = kWoodSnare;
                p.closedHat  = kClosed;
                p.openHat    = kOpen;
                p.ride       = kRide;
                p.crash      = kCrash;
                p.lowTom     = kLowFloor;
                p.midTom     = kLowTom;
                p.highTom    = kHighTom;
                p.velocityScale  = 1.02f;
                p.ghostBoost     = 0.5f;
                p.accentBoost    = 1.08f;
            }

            // -------- HARD ROCK ---------------------------------------------
            // Tama Rockstar: punchy tight kick, bright crashes, rock backbeat.
            {
                auto& p = v[(int) DrumKit::TamaRockstarHardRock];
                p = makeProfile();
                p.kick   = kTightKick;
                p.snare  = kWoodSnare;
                p.crash  = kCrash2;           // brighter crash
                p.ride   = kRide;
                p.velocityScale  = 1.00f;
                p.ghostBoost     = 0.7f;
                p.accentBoost    = 1.10f;
            }
            // Yamaha Studio hard rock: tight controlled kit, cleaner than Tama.
            {
                auto& p = v[(int) DrumKit::YamahaStudioHardRock];
                p = makeProfile();
                p.kick   = kTightKick;
                p.snare  = kWoodSnare;
                p.crash  = kCrash;
                p.ride   = kRide;
                p.velocityScale  = 0.98f;
                p.ghostBoost     = 0.75f;
                p.accentBoost    = 1.05f;
            }

            // -------- SHOEGAZE / GRUNGE / INDIE -----------------------------
            // DW Collector's Shoegaze (Superheaven-ish): washy open cymbals,
            // reverby snare, mid-velocity, lots of open hats.
            {
                auto& p = v[(int) DrumKit::DWCollectorsShoegaze];
                p = makeProfile();
                p.kick     = kBoomyKick;
                p.snare    = kWoodSnare;
                p.closedHat = kClosed;
                p.openHat  = kOpen;
                p.crash    = kCrash2;
                p.ride     = kRide2;          // washy ride
                p.velocityScale = 0.80f;
                p.ghostBoost    = 0.9f;
                p.accentBoost   = 0.95f;
            }
            // Pearl Masters Grunge (Nirvana-ish): loose tuning, raw, open hats.
            {
                auto& p = v[(int) DrumKit::PearlMastersGrunge];
                p = makeProfile();
                p.kick     = kBoomyKick;
                p.snare    = kWoodSnare;
                p.openHat  = kOpen;
                p.crash    = kCrash;
                p.velocityScale = 0.92f;
                p.ghostBoost    = 0.5f;
                p.accentBoost   = 1.08f;
            }
            // Yamaha Recording Custom Indie: dry clean kit, controlled accents.
            {
                auto& p = v[(int) DrumKit::YamahaRecordingIndie];
                p = makeProfile();
                p.kick    = kTightKick;
                p.snare   = kWoodSnare;
                p.crash   = kCrash;
                p.ride    = kRide;
                p.velocityScale = 0.86f;
                p.ghostBoost    = 0.8f;
                p.accentBoost   = 0.95f;
            }

            // -------- FUNK / R&B --------------------------------------------
            // Ludwig Black Beauty Funk: heavy ghost notes, sidestick rim clicks,
            // 16th-note hats, tight fat snare.
            {
                auto& p = v[(int) DrumKit::LudwigBlackBeautyFunk];
                p = makeProfile();
                p.kick       = kTightKick;
                p.snare      = kWoodSnare;
                p.ghostSnare = kSide;
                p.closedHat  = kClosed;
                p.openHat    = kOpen;
                p.velocityScale  = 0.88f;
                p.ghostBoost     = 1.8f;
                p.accentBoost    = 1.00f;
                p.ghostThreshold = 0.60f;
            }
            // Yamaha Live Custom Funk: open-hat accents, brighter snare.
            {
                auto& p = v[(int) DrumKit::YamahaLiveCustomFunk];
                p = makeProfile();
                p.kick      = kTightKick;
                p.snare     = kWoodSnare;
                p.openHat   = kOpen;
                p.velocityScale = 0.90f;
                p.ghostBoost    = 1.5f;
                p.accentBoost   = 1.05f;
            }
            // DW Performance R&B / Neo-soul: laid-back cross-stick, sub kick.
            {
                auto& p = v[(int) DrumKit::DWPerformanceRnB];
                p = makeProfile();
                p.kick       = kBoomyKick;    // sub-y boomy kick
                p.snare      = kSide;         // cross-stick as main feel
                p.ghostSnare = kSide;
                p.velocityScale  = 0.75f;
                p.ghostBoost     = 1.3f;
                p.accentBoost    = 0.92f;
                p.ghostThreshold = 0.65f;
            }

            // -------- METAL FAMILY ------------------------------------------
            // Sonor SQ2 Thrash (Metallica-ish): double-kick clicky kick, ride
            // bell accents, china-heavy fills, hot velocities.
            {
                auto& p = v[(int) DrumKit::SonorSQ2Thrash];
                p = makeProfile();
                p.kick      = kTightKick;     // trigger-clicky
                p.snare     = kElecSnare;     // tight, slightly gated
                p.crash     = kCrash;
                p.china     = kChina;
                p.rideBell  = kBell;
                p.velocityScale  = 1.05f;
                p.ghostBoost     = 0.3f;
                p.accentBoost    = 1.12f;
                p.preferChinaForFill = true;
            }
            // Tama Starclassic Metal (triggered): gated snare, click kick.
            {
                auto& p = v[(int) DrumKit::TamaStarclassicMetal];
                p = makeProfile();
                p.kick      = kTightKick;
                p.snare     = kElecSnare;
                p.china     = kChina;
                p.velocityScale  = 1.08f;
                p.ghostBoost     = 0.25f;
                p.accentBoost    = 1.15f;
                p.preferChinaForFill = true;
            }
            // Pearl Reference Metalcore: blast-beat friendly, china-heavy.
            {
                auto& p = v[(int) DrumKit::PearlReferenceMetalcore];
                p = makeProfile();
                p.kick      = kTightKick;
                p.snare     = kElecSnare;
                p.china     = kChina;
                p.crash     = kCrash2;
                p.velocityScale  = 1.06f;
                p.ghostBoost     = 0.4f;
                p.accentBoost    = 1.12f;
                p.preferChinaForFill = true;
            }
            // Yamaha PHX Prog Metal: cleaner, ride-heavy, polyrhythmic-friendly.
            {
                auto& p = v[(int) DrumKit::YamahaPHXProgMetal];
                p = makeProfile();
                p.kick      = kTightKick;
                p.snare     = kWoodSnare;     // less gated than thrash
                p.ride      = kRide;
                p.rideBell  = kBell;
                p.china     = kChina;
                p.velocityScale  = 1.00f;
                p.ghostBoost     = 0.6f;
                p.accentBoost    = 1.05f;
                p.preferChinaForFill = true;
            }

            // -------- COUNTRY -----------------------------------------------
            // Ludwig Classic Maple Country: brushed feel, sidestick accents,
            // train-beat friendly, sparse, low velocity.
            {
                auto& p = v[(int) DrumKit::LudwigClassicMapleCountry];
                p = makeProfile();
                p.kick       = kBoomyKick;
                p.snare      = kWoodSnare;
                p.ghostSnare = kSide;         // rim-click ghosts
                p.velocityScale  = 0.78f;
                p.ghostBoost     = 1.3f;
                p.accentBoost    = 0.95f;
                p.ghostThreshold = 0.62f;
            }

            // -------- ELECTRONIC / POP --------------------------------------
            // Roland TR-808 Hip-Hop: 808 sub kick + clap snare + tight closed hat.
            {
                auto& p = v[(int) DrumKit::Roland808HipHop];
                p = makeProfile();
                p.kick       = kBoomyKick;    // 808 sub — mapped to boomy slot
                p.snare      = kClap;         // 808 "snare" is really the clap
                p.ghostSnare = kClap;
                p.closedHat  = kClosed;
                p.openHat    = kOpen;
                p.clap       = kClap;
                p.velocityScale  = 0.95f;
                p.ghostBoost     = 0.4f;
                p.accentBoost    = 1.08f;
            }
            // Roland TR-909 Trap: hi-hat rolls, 808 sub, rim snare.
            {
                auto& p = v[(int) DrumKit::Roland909Trap];
                p = makeProfile();
                p.kick       = kBoomyKick;    // 808 sub
                p.snare      = kElecSnare;
                p.ghostSnare = kSide;
                p.closedHat  = kClosed;
                p.clap       = kClap;
                p.velocityScale  = 0.94f;
                p.ghostBoost     = 0.3f;
                p.accentBoost    = 1.10f;
            }
            // Akai Layered Pop: layered kick, clap snare, tight pocket.
            {
                auto& p = v[(int) DrumKit::AkaiLayeredPop];
                p = makeProfile();
                p.kick       = kTightKick;
                p.snare      = kWoodSnare;
                p.clap       = kClap;
                p.closedHat  = kClosed;
                p.velocityScale  = 0.95f;
                p.ghostBoost     = 0.6f;
                p.accentBoost    = 1.06f;
            }

            return v;
        }();

        const int idx = static_cast<int> (kit);
        if (idx < 0 || idx >= static_cast<int> (kProfiles.size()))
            return kProfiles[0];
        return kProfiles[(size_t) idx];
    }

    const std::vector<std::string>& drumKitDisplayNames()
    {
        static const std::vector<std::string> names {
            "Jazz \u2014 Ludwig Bebop",
            "Jazz \u2014 Gretsch Cool",
            "Classic Rock \u2014 Ludwig Supraphonic",
            "Classic Rock \u2014 Ludwig Vistalite '70s",
            "Hard Rock \u2014 Tama Rockstar",
            "Hard Rock \u2014 Yamaha Studio",
            "Shoegaze \u2014 DW Collector's",
            "Grunge \u2014 Pearl Masters",
            "Indie \u2014 Yamaha Recording Custom",
            "Funk \u2014 Ludwig Black Beauty",
            "Funk \u2014 Yamaha Live Custom",
            "R&B / Neo-soul \u2014 DW Performance",
            "Thrash Metal \u2014 Sonor SQ2",
            "Metal \u2014 Tama Starclassic Triggered",
            "Metalcore \u2014 Pearl Reference",
            "Prog Metal \u2014 Yamaha PHX",
            "Country \u2014 Ludwig Classic Maple",
            "Hip-Hop \u2014 Roland TR-808",
            "Trap \u2014 Roland TR-909 + 808",
            "Pop \u2014 Akai Layered Sample"
        };
        return names;
    }
}
