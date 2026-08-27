#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/PauseLayer.hpp>

using namespace geode::prelude;

class $modify(DFPauseLayer, PauseLayer) {
    void customSetup() {
        log::info("DF and orbs in Pause Menu: PauseLayer::customSetup hook reached before original.");

        PauseLayer::customSetup();

        log::info("DF and orbs in Pause Menu: PauseLayer::customSetup hook completed.");
    }
};

$on_mod(Loaded) {
    log::info("DF and orbs in Pause Menu loaded successfully with Geode $modify PauseLayer hook.");
}
