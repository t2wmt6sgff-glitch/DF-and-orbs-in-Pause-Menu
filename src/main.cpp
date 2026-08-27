#include <Geode/Geode.hpp>
#include <Geode/loader/Tulip.hpp>

using namespace geode::prelude;

namespace {
    void pauseLayerCustomSetup(PauseLayer* self) {
        using CustomSetup = void(*)(PauseLayer*);

        static CustomSetup original = [] {
            auto wrapper = geode::hook::createWrapper(
                reinterpret_cast<void*>(geode::base::get() + 0x141df8),
                tulip::hook::WrapperMetadata{
                    .m_convention = geode::hook::createConvention(tulip::hook::TulipConvention::Default),
                    .m_abstract = tulip::hook::AbstractFunction::from(CustomSetup{})
                }
            );

            if (wrapper.isErr()) {
                log::error("Failed to create PauseLayer::customSetup wrapper: {}", wrapper.unwrapErr());
                return static_cast<CustomSetup>(nullptr);
            }

            return reinterpret_cast<CustomSetup>(wrapper.unwrap());
        }();

        if (!original) {
            log::error("PauseLayer::customSetup original is unavailable; skipping hook body.");
            return;
        }

        original(self);
        log::info("DF and orbs in Pause Menu: PauseLayer::customSetup reached.");
    }
}

$on_mod(Loaded) {
    auto hook = Mod::get()->hook(
        reinterpret_cast<void*>(geode::base::get() + 0x141df8),
        &pauseLayerCustomSetup,
        "PauseLayer::customSetup",
        tulip::hook::TulipConvention::Default
    );

    if (hook.isErr()) {
        log::error("Failed to install PauseLayer::customSetup hook: {}", hook.unwrapErr());
        return;
    }

    log::info("DF and orbs in Pause Menu loaded successfully.");
}
