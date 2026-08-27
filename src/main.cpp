#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    log::info("DF and orbs in Pause Menu loaded successfully.");
}

class $modify(DFPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        log::info("DF and orbs in Pause Menu: PauseLayer::customSetup reached.");
    }
};
