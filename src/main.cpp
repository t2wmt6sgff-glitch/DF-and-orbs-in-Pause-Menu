#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace {
    constexpr float kDifficultyFaceScale = 0.55f;

    char const* difficultyName(int difficulty) {
        switch (difficulty) {
            case -1: return "Unrated";
            case 0: return "Auto";
            case 1: return "Easy";
            case 2: return "Normal";
            case 3: return "Hard";
            case 4: return "Harder";
            case 5: return "Insane";
            case 6: return "Hard Demon";
            case 7: return "Easy Demon";
            case 8: return "Medium Demon";
            case 9: return "Insane Demon";
            case 10: return "Extreme Demon";
            default: return "Unknown";
        }
    }

    int difficultyForLevel(GJGameLevel* level) {
        if (level->m_autoLevel) {
            return static_cast<int>(GJDifficulty::Auto);
        }

        auto const rawDifficulty = static_cast<int>(level->m_difficulty);

        // Official main demons are all hard demons. Their demon subtype can retain
        // the default value used by a newly constructed GJGameLevel.
        if (level->m_levelType == GJLevelType::Main &&
            rawDifficulty == static_cast<int>(GJDifficulty::Demon)) {
            return static_cast<int>(GJDifficulty::Demon);
        }

        if (level->m_demon.value() != 0) {
            return GJGameLevel::demonIconForDifficulty(
                static_cast<DemonDifficultyType>(level->m_demonDifficulty)
            );
        }

        // Online levels may carry the voters' average separately from the cached
        // enum. GD exposes the same calculation through getAverageDifficulty().
        if (level->m_ratingsSum != 0) {
            auto const averageDifficulty = level->getAverageDifficulty();
            if (averageDifficulty >= static_cast<int>(GJDifficulty::Easy) &&
                averageDifficulty <= static_cast<int>(GJDifficulty::Insane)) {
                return averageDifficulty;
            }
        }

        if (rawDifficulty >= static_cast<int>(GJDifficulty::NA) &&
            rawDifficulty <= static_cast<int>(GJDifficulty::DemonExtreme)) {
            // GJGameLevel defaults to Auto even before a level has a rating. The
            // explicit m_autoLevel flag is what distinguishes a real Auto level.
            if (rawDifficulty == static_cast<int>(GJDifficulty::Auto)) {
                return static_cast<int>(GJDifficulty::NA);
            }
            return rawDifficulty;
        }

        return static_cast<int>(GJDifficulty::NA);
    }

    void addDifficultyFace(PauseLayer* pauseLayer) {
        if (pauseLayer->getChildByID("difficulty-face"_spr)) {
            log::warn("DF and orbs in Pause Menu: difficulty face already exists; skipping duplicate.");
            return;
        }

        auto* playLayer = PlayLayer::get();
        if (!playLayer || !playLayer->m_level) {
            log::warn("DF and orbs in Pause Menu: no current GJGameLevel was available; difficulty face omitted.");
            return;
        }

        auto* level = playLayer->m_level;
        auto const rawDifficulty = static_cast<int>(level->m_difficulty);
        auto const demonValue = level->m_demon.value();
        auto const difficulty = difficultyForLevel(level);

        log::info(
            "DF and orbs in Pause Menu: current level found (raw difficulty={}, auto={}, demon={}, demon subtype={}); resolved difficulty={} ({}).",
            rawDifficulty,
            level->m_autoLevel,
            demonValue,
            level->m_demonDifficulty,
            difficulty,
            difficultyName(difficulty)
        );

        if (difficulty < static_cast<int>(GJDifficulty::NA) ||
            difficulty > static_cast<int>(GJDifficulty::DemonExtreme)) {
            log::warn("DF and orbs in Pause Menu: resolved difficulty {} is outside GD's supported range; face omitted.", difficulty);
            return;
        }

        auto const frameName = GJDifficultySprite::getDifficultyFrame(
            difficulty,
            GJDifficultyName::Short
        );

        log::info("DF and orbs in Pause Menu: attempting original GD difficulty frame '{}'.", frameName.c_str());

        if (frameName.empty() ||
            !CCSpriteFrameCache::get()->spriteFrameByName(frameName.c_str())) {
            log::warn("DF and orbs in Pause Menu: original GD difficulty frame '{}' was unavailable; face omitted.", frameName.c_str());
            return;
        }

        auto* difficultyFace = GJDifficultySprite::create(
            difficulty,
            GJDifficultyName::Short
        );
        if (!difficultyFace) {
            log::warn("DF and orbs in Pause Menu: GJDifficultySprite creation failed for '{}'; face omitted.", frameName.c_str());
            return;
        }

        auto layerSize = pauseLayer->getContentSize();
        if (layerSize.width <= 0.f || layerSize.height <= 0.f) {
            layerSize = CCDirector::get()->getWinSize();
        }

        auto const faceX = std::clamp(layerSize.width * 0.08f, 36.f, 48.f);
        auto faceY = layerSize.height - 32.f;
        if (auto* levelName = pauseLayer->getChildByID("level-name")) {
            faceY = levelName->getPositionY();
        }

        difficultyFace->setID("difficulty-face"_spr);
        difficultyFace->setScale(kDifficultyFaceScale);
        difficultyFace->setAnchorPoint({0.5f, 0.5f});
        difficultyFace->setPosition({faceX, faceY});
        pauseLayer->addChild(difficultyFace, 10);

        log::info(
            "DF and orbs in Pause Menu: difficulty face created successfully at ({}, {}) with scale {}.",
            faceX,
            faceY,
            kDifficultyFaceScale
        );
    }
}

class $modify(DFPauseLayer, PauseLayer) {
    void customSetup() {
        log::info("DF and orbs in Pause Menu: PauseLayer::customSetup hook reached before original.");

        PauseLayer::customSetup();

        addDifficultyFace(this);

        log::info("DF and orbs in Pause Menu: PauseLayer::customSetup hook completed.");
    }
};

$on_mod(Loaded) {
    log::info("DF and orbs in Pause Menu loaded successfully with Geode $modify PauseLayer hook.");
}
